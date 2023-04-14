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
	
	enum progress_type {
		PROG_MESSAGE,
		PROG_BAR,
	};
	
	// If progress_type is PROG_MESSAGE, only msg is valid, and the off_t's are 0.
	// If progress_type is PROG_BAR, then progress represents a progress bar, bytes_total refers to amount of bytes to be fetch, and bytes_now refers to amount of bytes we have fetched so far. Multiple PROG_BAR's will be sent until the transfer is completed. Only the first PROG_BAR of the sequence will contain a message.
	// If total is unknown, (is common at the beginning of downloads), bytes_total will be 0, be prepared to handle this case.
	// At the end of a PROG_BAR sequence, a PROG_MESSAGE with msg.empty() == true will be sent.
	using progress_function = void(progress_type, std::string msg, off_t bytes_total, off_t bytes_now);
	
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
	
	template <typename Fn>
	struct scope_guard {
		scope_guard(Fn fn) : fn(std::move(fn)) {}
		
		~scope_guard() {
			fn();
		}
		
	private:
		Fn fn;
	};
	
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
		
		void set_ugoira(fs::path ugoira) {
			param_ugoira = std::move(ugoira);
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
		
		void set_zip(fs::path zip) {
			param_zip = std::move(zip);
		}
		
		result convert(const fs::path &dest, format fmt) {
			setup_temp_dir();
			
			scope_guard sg = [this] {
				teardown_temp_dir();
				param_post_id = {};
				param_ugoira = {};
				param_meta = {};
				param_zip = {};
			};
			
			bool unzipped = false;
			auto frames_path = temp_dir / "frames";
			
			if (param_ugoira) {
				set_zip(*param_ugoira);
				
				if (auto res = unzip(*param_zip, frames_path); !res) {
					return res;
				}
				
				unzipped = true;
				auto meta_path = frames_path / "animation.json";
				
				if (!fs::exists(meta_path)) {
					return {ERR_META_CANTOPEN, "Ugoira file does not contain an animation.json"};
				}
				
				if (auto res = set_meta(meta_path); !res) {
					return res;
				}
			}
			
			if (!param_meta) {
				if (!param_post_id) {
					return {ERR_USAGE, "Post ID must be given if meta file is not"};
				}
				
				progress(0, 0, "Downloading ugoira_meta");
				
				auto url = "https://www.pixiv.net/ajax/illust/" + std::to_string(*param_post_id) + "/ugoira_meta?lang=en";
				auto resp = pixiv_request(url, true);
				
				if (auto r = set_meta(std::string_view{resp.body}); r.err != ERR_OK) {
					if (resp.code != 200) {
						return {ERR_REQ_FAILED, "Failed to fetch ugoira meta info: " + gen_err_message(resp)};
					}
					
					return r;
				}
			}
			
			if (param_meta->contains("error") && param_meta->at("error").get<bool>()) {
				return {ERR_REQ_FAILED, "Pixiv: " + param_meta->at("message").get<std::string>()};
			}
			
			auto mi = get_meta_info(*param_meta);
			
			if (!mi) {
				return {ERR_META_INVALID, "Invalid meta file (missing fields or wrong data types)"};
			}
			
			if (!param_zip) {
				progress(0, 0, "Downloading ugoira.zip");
				
				auto resp = pixiv_request(mi->zip_url, true);
				
				if (resp.code != 200) {
					return {ERR_REQ_FAILED, "Failed to fetch ugoira frames (zip): " + gen_err_message(resp)};
				}
				
				auto zip_path = temp_dir / "ugoira.zip";
				std::ofstream out{zip_path};
				out.write(resp.body.data(), resp.body.size());
				param_zip = zip_path;
			}
			
			if (!unzipped) {
				if (auto res = unzip(*param_zip, frames_path); !res) {
					return res;
				}
			}
			
			return do_convert(*mi, frames_path, dest, fmt);
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
		
		void show_progress(bool yn) {
			showprogress = yn;
		}
		
		void set_progressfn(std::function<progress_function> fn) {
			progressfn = std::move(fn);
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
		
		result unzip(const fs::path &zip, const fs::path &dest) {
			if (!fs::exists(zip)) {
				return {ERR_ZIP_CANTOPEN, "File doesn't exist: " + zip.native()};
			}
			
			fs::create_directory(dest);
			
			if (!run_unzip(zip, dest)) {
				return {ERR_CMD_FAILED, "unzip command failed"};
			}
			
			return {};
		}
		
		std::optional<meta_info> get_meta_info(const json &meta) {
			meta_info mi;
			
			try {
				// Meta files fetched directly from pixiv are wrapped in more JSON containing the error code,
				// and the actual meta file is in body. But PixivUtil2 only outputs the value of body without
				// the wrapping JSON, so support both cases.
				auto &body = meta.contains("body") ?
				             meta.at("body") :
				             meta;
				
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
					out << "duration ";
					
					if (fmt == FMT_WEBM) {
						out << f.delay;
					}
					else {
						out << (f.delay / 1000.f);
					}
					
					out << '\n';
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
			
			ss << "-fflags bitexact ";
			ss << "-vsync " << (fs.is_constant ? "cfr" : "vfr") << ' ';
			
			float fps_limit = (fmt == FMT_GIF ? 50.f : 60.f);
			
			if (fs.is_constant) {
				ss << "-r " << std::min(fs.const_fps, fps_limit) << ' ';
			}
			
			if (fmt == FMT_WEBM && !fs.is_constant) {
				ss << "-enc_time_base 1/1000 -vf 'settb=1/1000,setpts=PTS*0.001' ";
			}
			
			ss << '\'' << dest.string() << '\'';
			
			return ss.str();
		}
		
		result do_convert(const meta_info &mi, const fs::path &frames_path, const fs::path &dest, format fmt) {
			assert(!temp_dir.empty());
			
			auto fs = get_frame_stats(mi);
			
			auto concat_path = temp_dir / "ffmpeg_input.txt";
			create_concat_file(frames_path, mi, fs, fmt, concat_path);
			
			auto dest_part = dest + ".part";
			auto cmd = gen_convert_cmd(concat_path, dest_part, fmt, fs);
			
			progress("Encoding to " + extension(fmt));
			
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
		
		void setup_temp_dir() {
			std::mt19937 rng{std::random_device{}()};
			
			do {
				auto str = gen_random_string(32, rng);
				auto base = fs::temp_directory_path() / "ugoira-convert";
				fs::create_directories(base);
				temp_dir = base / str;
			} while (!fs::create_directory(temp_dir));
		}
		
		void teardown_temp_dir() {
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
		
		response pixiv_request(std::string_view url, bool prog = false) {
			auto cookies = gen_cookies();
			
			request_opts opts;
			opts.referer = "https://www.pixiv.net/";
			opts.user_agent = user_agent;
			opts.cookies = cookies;
			
			if (prog) {
				opts.progressfn = [this](off_t total, off_t now) {
					progress(total, now);
				};
			}
			
			auto r = req->get(url, opts);
			
			if (prog) {
				// libcurl for some reason doesn't send a final progress event to make it 100%. so send it ourselves.
				progress(r.body.size(), r.body.size());
				// and then signal the end of the operation.
				progress({});
			}
			
			return r;
		}
		
		static std::string gen_err_message(const response &resp) {
			if (resp.code == 0) {
				return resp.message;
			}
			
			return "Request returned " + std::to_string(resp.code);
		}
		
		void progress(std::string msg) {
			if (showprogress && progressfn) {
				progressfn(PROG_MESSAGE, std::move(msg), 0, 0);
			}
		}
		
		void progress(off_t total, off_t now, std::string msg = {}) {
			if (showprogress && progressfn) {
				progressfn(PROG_BAR, std::move(msg), total, now);
			}
		}
		
		bool showprogress = true;
		std::function<progress_function> progressfn;
		
		std::string user_agent = "Mozilla/5.0 (X11; Linux x86_64; rv:91.0) Gecko/20100101 Firefox/91.0";
		std::string session_id;
		
		std::optional<uint64_t> param_post_id;
		std::optional<fs::path> param_ugoira;
		std::optional<json> param_meta;
		std::optional<fs::path> param_zip;
		
		fs::path temp_dir;
		
		std::unique_ptr<requester> default_requester;
		requester *req = nullptr;
	};
}
