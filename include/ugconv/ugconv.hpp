#pragma once

#include <iostream>
#include <fstream>
#include <sstream>
#include <string_view>
#include <string>
#include <filesystem>
#include <random>
#include <optional>
#include <memory>
#include <charconv>
#include <assert.h>
#include <stdint.h>
#include <nlohmann/json.hpp>
#include <ugconv/request.hpp>

#ifndef UGCONV_NO_CURL
#include <ugconv/curl.hpp>
#endif

namespace ugconv {
	namespace fs = std::filesystem;
	using nlohmann::json;
	
	enum errcode {
		ERR_OK = 0,
		ERR_USAGE,
		ERR_CMD_FAILED,
		ERR_META_CANTOPEN,
		ERR_ZIP_CANTOPEN,
		ERR_META_INVALID,
		ERR_REQ_FAILED,
		ERR_URL_INVALID,
	};
	
	struct result {
		errcode err = ERR_OK;
		std::string message;
		
		constexpr operator bool () const {
			return err == ERR_OK;
		}
	};
	
	enum format {
		FMT_GIF,
		FMT_WEBM,
	};
	
	inline fs::path operator+(fs::path a, std::string_view b) {
		a += b;
		return a;
	}
	
	template <std::integral I>
	std::optional<I> chars_to_int(std::string_view s) {
		I x{};
		auto [p, e] = std::from_chars(s.begin(), s.end(), x);
		
		if (e == std::errc::invalid_argument || p != s.end()) {
			return {};
		}
		
		return x;
	}
	
	constexpr std::string_view extension(format fmt) {
		switch (fmt) {
			case FMT_GIF:
				return "gif";
			case FMT_WEBM:
				return "webm";
		}
		
		assert(false);
	}
	
	constexpr std::optional<format> parse_format(std::string_view ext) {
		if (ext == "gif") {
			return FMT_GIF;
		}
		else if (ext == "webm") {
			return FMT_WEBM;
		}
		
		return {};
	}
	
	struct context {
		context(requester &req) : req(&req) {}
		
#ifndef UGCONV_NO_CURL
		context() : default_requester(std::make_unique<curl>()), req(default_requester.get()) {}
#else
		context() = default;
#endif
		
		result set_post(std::string_view url) {
			static constexpr std::string_view base = "www.pixiv.net/en/artworks/";
			
			if (url.starts_with("https://")) {
				url = url.substr(8);
			}
			else if (url.starts_with("http://")) {
				url = url.substr(7);
			}
			
			if (!url.starts_with(base)) {
				return {ERR_URL_INVALID, "Invalid artwork URL (must be in the form [http(s)://]" + base + "<ID>"};
			}
			
			param_post_id = chars_to_int<uint64_t>(url.substr(base.size()));
			
			if (!param_post_id) {
				return {ERR_URL_INVALID, "Invalid artwork URL (ID is not a non-negative integer)"};
			}
			
			return {};
		}
		
		void set_post(uint64_t id) {
			param_post_id = id;
		}
		
		result set_meta(const fs::path &meta) {
			std::ifstream in{meta};
			
			if (!in) {
				return {ERR_META_CANTOPEN, "Failed to open meta file: " + meta.string()};
			}
			
			return set_meta(in);
		}
		
		result set_meta(std::istream &meta) {
			try {
				param_meta = json::parse(meta);
			}
			catch (std::exception &e) {
				return {ERR_META_INVALID, "Failed to parse JSON meta file: " + std::string{e.what()}};
			}
			
			return {};
		}
		
		result set_meta(std::string_view meta) {
			try {
				param_meta = json::parse(meta);
			}
			catch (std::exception &e) {
				return {ERR_META_INVALID, "Failed to parse JSON meta file: " + std::string{e.what()}};
			}
			
			return {};
		}
		
		void set_zip(const fs::path &zip) {
			param_zip = zip;
		}
		
		result convert(const fs::path &dest, format fmt) {
			auto tdg = tempdir_guard();
			
			if (!param_meta) {
				if (!param_post_id) {
					return {ERR_USAGE, "Post ID must be given if meta file is not"};
				}
				
				auto url = "https://www.pixiv.net/ajax/illust/" + std::to_string(*param_post_id) + "/ugoira_meta?lang=en";
				auto resp = pixiv_request(url);
				
				if (auto r = set_meta(std::string_view{resp.body}); r.err != ERR_OK) {
					if (resp.code != 200) {
						return {ERR_REQ_FAILED, "Failed to fetch ugoira meta info: " + gen_err_message(resp)};
					}
					
					return r;
				}
			}
			
			if (param_meta->at("error").get<bool>()) {
				return {ERR_REQ_FAILED, "Pixiv: " + param_meta->at("message").get<std::string>()};
			}
			
			auto mi = get_meta_info(*param_meta);
			
			if (!mi) {
				return {ERR_META_INVALID, "Invalid meta file (missing fields or wrong data types)"};
			}
			
			if (!param_zip) {
				auto resp = pixiv_request(mi->zip_url);
				
				if (resp.code != 200) {
					return {ERR_REQ_FAILED, "Failed to fetch ugoira frames (zip): " + gen_err_message(resp)};
				}
				
				auto zip_path = temp_dir / "ugoira.zip";
				std::ofstream out{zip_path};
				out.write(resp.body.data(), resp.body.size());
				param_zip = zip_path;
			}
			
			return do_convert(*mi, *param_zip, dest, fmt);
		}
		
		void set_user_agent(std::string ua) {
			user_agent = std::move(ua);
		}
		
		void set_session_id(std::string sid) {
			session_id = std::move(sid);
		}
		
		auto post_id() const {
			return param_post_id;
		}
		
	private:
		struct frame {
			std::string name;
			int delay = 0;
		};
		
		struct meta_info {
			std::string zip_url;
			std::vector<frame> frames;
		};
		
		struct tempdir_guard_struct {
			tempdir_guard_struct(context &ctx) : ctx(&ctx) {
				ctx.ref_temp_dir();
			}
			
			~tempdir_guard_struct() {
				ctx->unref_temp_dir();
			}
			
		private:
			context *ctx;
		};
		
		tempdir_guard_struct tempdir_guard() {
			return tempdir_guard_struct{*this};
		}
		
		std::optional<meta_info> get_meta_info(const json &meta) {
			meta_info mi;
			
			try {
				auto &body = meta.at("body");
				body.at("originalSrc").get_to(mi.zip_url);
				
				auto &frames = body.at("frames");
				
				for (auto &f : frames) {
					auto &frame = mi.frames.emplace_back();
					f.at("file").get_to(frame.name);
					f.at("delay").get_to(frame.delay);
				}
				
				return std::move(mi);
			}
			catch (...) {
				return {};
			}
		}
		
		struct frame_stats {
			float avg_fps;
			bool is_constant;
			float const_fps;
		};
		
		frame_stats get_frame_stats(const meta_info &mi) {
			frame_stats fs;
			
			auto first_delay = mi.frames.front().delay;
			
			fs.is_constant = true;
			fs.const_fps = 0;
			
			int sum = 0;
			
			for (const auto &f : mi.frames) {
				if (f.delay != first_delay) {
					fs.is_constant = false;
				}
				
				sum += f.delay;
			}
			
			if (fs.is_constant) {
				fs.const_fps = 1000.f / first_delay;
			}
			
			fs.avg_fps = 1000.f / (float(sum) / mi.frames.size());
			
			return fs;
		}
		
		void create_concat_file(const fs::path &frames_path, const meta_info &mi, const frame_stats &fs, format fmt, const fs::path &outpath) {
			std::ofstream out{outpath};
			assert(out);
			
			out << std::fixed;
			
			for (const auto &f : mi.frames) {
				out << "file '" << (frames_path / f.name).string() << "'\n";
				
				if (!fs.is_constant) {
					out << "duration " << (f.delay / 1000.f) << '\n';
				}
			}
			
			if (fmt == FMT_WEBM && fs.avg_fps < 5) {
				out << "file '" << (frames_path / mi.frames.back().name).string() << "'\n";
			}
		}
		
		static std::string gen_convert_cmd(const fs::path &concat, const fs::path &dest, format fmt, const frame_stats &fs) {
			std::stringstream ss;
			
			ss << "ffmpeg -loglevel error -y -f concat -safe 0 ";
			
			if (fs.is_constant) {
				ss << "-r " << fs.const_fps << ' ';
			}
			
			ss << "-i '" << concat.string() << "' ";
			
			if (fmt == FMT_GIF) {
				ss << "-vf 'split[s0][s1];[s0]palettegen[p];[s1][p]paletteuse=dither=sierra2' -f gif ";
			}
			else if (fmt == FMT_WEBM) {
				ss << "-f webm -c:v libvpx -b:v 10M -crf 4 ";
			}
			
			ss << "-vsync " << (fs.is_constant ? "cfr" : "vfr") << ' ';
			
			int fps_limit = (fmt == FMT_GIF ? 50 : 60);
			
			if (fs.is_constant && fs.const_fps <= fps_limit) {
				ss << "-r " << fs.const_fps << ' ';
			}
			else {
				ss << "-r " << fps_limit << ' ';
			}
			
			ss << '\'' << dest.string() << '\'';
			
			return ss.str();
		}
		
		result do_convert(const meta_info &mi, const fs::path &zip, const fs::path &dest, format fmt) {
			auto tdg = tempdir_guard();
			auto frames_path = temp_dir / "frames";
			
			if (!fs::exists(zip)) {
				return {ERR_ZIP_CANTOPEN, "Zip file doesn't exist: " + zip.string()};
			}
			
			fs::create_directory(frames_path);
			
			if (!run_unzip(zip, frames_path)) {
				return {ERR_CMD_FAILED, "unzip command failed"};
			}
			
			auto fs = get_frame_stats(mi);
			
			auto concat_path = temp_dir / "ffmpeg_input.txt";
			create_concat_file(frames_path, mi, fs, fmt, concat_path);
			
			auto dest_part = dest + ".part";
			auto cmd = gen_convert_cmd(concat_path, dest_part, fmt, fs);
			
			if (!runshell(std::move(cmd))) {
				return {ERR_CMD_FAILED, "ffmpeg command failed"};
			}
			
			fs::rename(dest_part, dest);
			
			return {};
		}
		
		static bool runshell(std::string cmd) {
			return system(cmd.c_str()) == 0;
		}
		
		static bool run_unzip(fs::path zip, fs::path dest) {
			std::stringstream ss;
			ss << "unzip -q " << '\'' << zip.string() << '\'' << " -d " << '\'' << dest.string() << '\'';
			return runshell(ss.str());
		}
		
		std::string gen_random_string(size_t len, auto &rng) {
			std::string out;
			out.resize(len);
			
			std::string_view chars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890";
			std::uniform_int_distribution distr{size_t(0), chars.size() - 1};
			
			for (auto &c : out) {
				c = chars[distr(rng)];
			}
			
			return out;
		}
		
		void ref_temp_dir() {
			temp_refs++;
			
			if (temp_refs > 1) {
				return;
			}
			
			std::mt19937 rng{std::random_device{}()};
			
			do {
				auto str = gen_random_string(32, rng);
				auto base = fs::temp_directory_path() / "ugoira-convert";
				fs::create_directories(base);
				temp_dir = base / str;
			} while (!fs::create_directory(temp_dir));
		}
		
		void unref_temp_dir() {
			temp_refs--;
			
			if (temp_refs > 0) {
				return;
			}
			
			if (temp_dir.empty()) {
				return;
			}
			
			fs::remove_all(temp_dir);
		}
		
		std::string gen_cookies() {
			std::stringstream ss;
			
			if (!session_id.empty()) {
				ss << "PHPSESSID=" << session_id;
			}
			
			return ss.str();
		}
		
		response pixiv_request(std::string_view url) {
			auto cookies = gen_cookies();
			
			request_opts opts;
			opts.referer = "https://www.pixiv.net/";
			opts.user_agent = user_agent;
			opts.cookies = cookies;
			
			return req->get(url, opts);
		}
		
		static std::string gen_err_message(const response &resp) {
			if (resp.code == 0) {
				return resp.message;
			}
			
			return "Request returned " + std::to_string(resp.code);
		}
		
		std::string user_agent = "Mozilla/5.0 (X11; Linux x86_64; rv:91.0) Gecko/20100101 Firefox/91.0";
		std::string session_id;
		
		std::optional<uint64_t> param_post_id;
		std::optional<json> param_meta;
		std::optional<fs::path> param_zip;
		
		fs::path temp_dir;
		unsigned temp_refs = 0;
		
		std::unique_ptr<requester> default_requester;
		requester *req = nullptr;
	};
}
