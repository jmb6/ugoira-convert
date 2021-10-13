#include <ugconv/ugconv.hpp>

#include <iostream>
#include <vector>
#include <string>
#include <string_view>
#include <unordered_map>

namespace fs = std::filesystem;

struct option_info {
	bool has_arg;
};

static std::unordered_map<std::string_view, option_info> option_spec = {
	{"-u", {true}},
	{"-s", {true}},
	{"-fmt", {true}},
	{"-ugoira", {true}},
	{"-meta", {true}},
	{"-zip", {true}},
	{"-id", {true}},
	{"-q", {false}},
};

struct options {
	std::unordered_map<std::string_view, std::string_view> flags;
	std::vector<std::string_view> args;
};

template <typename U>
constexpr auto find(auto &map, U &&key) {
	auto iter = map.find(std::forward<U>(key));
	
	if (iter != map.end()) {
		return &iter->second;
	}
	
	return static_cast<decltype(&iter->second)>(nullptr);
}

static std::string operator+(std::string a, std::string_view b) {
	a.append(b.data(), b.size());
	return a;
}

static options parse_options(int argc, char **argv) {
	options opts;
	
	for (int i = 1; i < argc; i++) {
		auto arg = std::string_view{argv[i]};
		
		if (auto spec = find(option_spec, arg)) {
			std::string_view flagarg;
			
			if (spec->has_arg) {
				if (i == argc - 1) {
					std::cout << arg << " requires an argument\n";
					exit(1);
				}
				
				flagarg = argv[++i];
			}
			
			opts.flags[arg] = flagarg;
		}
		else if (arg.starts_with("-")) {
			std::cout << "Unknown flag " << arg << '\n';
			exit(1);
		}
		else {
			opts.args.emplace_back(arg);
		}
	}
	
	return opts;
}

static ugconv::format determine_format(const fs::path &out, std::string_view *fmtflag) {
	if (fmtflag) {
		auto fmt = ugconv::parse_format(*fmtflag);
		
		if (!fmt) {
			std::cout << "Unrecognized format " << *fmtflag << '\n';
			exit(1);
		}
		
		return *fmt;
	}
	
	if (out.has_extension()) {
		auto ext = out.extension().string();
		auto fmt = ugconv::parse_format(std::string_view{ext}.substr(1));
		
		if (!fmt) {
			std::cout << "Unrecognized extension " << ext << '\n';
			exit(1);
		}
		
		return *fmt;
	}
	
	return ugconv::FMT_WEBM;
}

int main(int argc, char **argv) {
	auto opts = parse_options(argc, argv);
	
	if (auto sid = getenv("UGCONV_SESSION_ID"); sid && !find(opts.flags, "-s")) {
		opts.flags["-s"] = sid;
	}
	
	ugconv::context ctx;
	
	if (auto ua = find(opts.flags, "-u")) {
		ctx.set_user_agent(std::string{*ua});
	}
	
	if (auto s = find(opts.flags, "-s")) {
		ctx.set_session_id(std::string{*s});
	}
	
	bool have_ugoira = false;
	
	if (auto ugoira = find(opts.flags, "-ugoira")) {
		ctx.set_ugoira(fs::path{*ugoira});
		have_ugoira = true;
	}
	
	bool have_meta = false;
	
	if (auto meta = find(opts.flags, "-meta")) {
		if (have_ugoira) {
			std::cout << "-meta doesn't make sense with -ugoira\n";
			return 1;
		}
		
		if (auto res = ctx.set_meta(fs::path{*meta}); !res) {
			std::cout << res.message << '\n';
			return 1;
		}
		
		have_meta = true;
	}
	
	if (auto zip = find(opts.flags, "-zip")) {
		if (!have_meta) {
			std::cout << "-zip can only be supplied if -meta is supplied as well\n";
			return 1;
		}
		
		ctx.set_zip(*zip);
	}
	
	bool have_id = false;
	
	if (auto idstr = find(opts.flags, "-id")) {
		if (have_ugoira) {
			std::cout << "-id doesn't make sense with -ugoira\n";
			return 1;
		}
		
		if (have_meta) {
			std::cout << "-id doesn't make sense with -meta\n";
			return 1;
		}
		
		auto id = ugconv::chars_to_int<uint64_t>(*idstr);
		
		if (!id) {
			std::cout << "-id should be a non-negative integer\n";
			return 1;
		}
		
		ctx.set_post(*id);
		have_id = true;
	}
	
	size_t arg_enum = 0;
	
	if (!have_meta && !have_id && !have_ugoira) {
		if (opts.args.empty()) {
			std::cout << "Expected arguments\n";
			return 1;
		}
		
		auto url = opts.args[arg_enum++];
		
		if (auto res = ctx.set_post(url); !res) {
			std::cout << res.message << '\n';
			return 1;
		}
	}
	
	fs::path out;
	
	if (arg_enum + 1 <= opts.args.size()) {
		out = opts.args[arg_enum++];
	}
	
	auto fmt = determine_format(out, find(opts.flags, "-fmt"));
	
	if (out.empty() || fs::is_directory(out)) {
		auto ext = ugconv::extension(fmt);
		fs::path name;
		
		if (auto pid = ctx.post_id()) {
			name = std::to_string(*pid) + '.' + ext;
		}
		else {
			name = "out." + ext;
		}
		
		if (fs::is_directory(out)) {
			out /= std::move(name);
		}
		else {
			out = std::move(name);
		}
	}
	
	std::string progbar_msg;
	
	ctx.set_progressfn([&progbar_msg](auto type, auto msg, auto total, auto now) {
		if (type == ugconv::PROG_MESSAGE) {
			std::cout << msg << '\n';
		}
		else if (type == ugconv::PROG_BAR) {
			if (!msg.empty()) {
				progbar_msg = std::move(msg);
			}
			
			int percent = total ? ((float(now) / total) * 100) : 0;
			std::cout << "\r[" << percent << "%] " << progbar_msg << std::flush;
		}
	});
	
	ctx.show_progress(!opts.flags.contains("-q"));
	
	if (auto res = ctx.convert(out, fmt); !res) {
		std::cout << res.message << '\n';
		return 1;
	}
}
