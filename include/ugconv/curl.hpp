#pragma once

#include <memory>
#include <curl/curl.h>
#include <assert.h>

namespace ugconv {
	struct curl final : requester {
		curl() : errbuf(std::make_unique<char[]>(CURL_ERROR_SIZE)) {
			ctx = curl_easy_init();
			assert(ctx);
		}
		
		response get(std::string_view url, const request_opts &opts) override {
			response resp;
			
			curl_easy_setopt(ctx, CURLOPT_URL, std::string{url}.c_str());
			curl_easy_setopt(ctx, CURLOPT_WRITEFUNCTION, writefunction);
			curl_easy_setopt(ctx, CURLOPT_WRITEDATA, &resp.body);
			curl_easy_setopt(ctx, CURLOPT_ERRORBUFFER, errbuf.get());
			curl_easy_setopt(ctx, CURLOPT_REFERER, std::string{opts.referer}.c_str());
			curl_easy_setopt(ctx, CURLOPT_COOKIE, std::string{opts.cookies}.c_str());
			curl_easy_setopt(ctx, CURLOPT_USERAGENT, std::string{opts.user_agent}.c_str());
			curl_easy_setopt(ctx, CURLOPT_NOPROGRESS, 0);
			curl_easy_setopt(ctx, CURLOPT_XFERINFOFUNCTION, progressfunction);
			curl_easy_setopt(ctx, CURLOPT_XFERINFODATA, &opts);
			
			auto err = curl_easy_perform(ctx);
			
			curl_easy_getinfo(ctx, CURLINFO_RESPONSE_CODE, &resp.code);
			
			if (err != CURLE_OK) {
				resp.message = errbuf.get();
			}
			
			return resp;
		}
		
		~curl() {
			if (ctx) {
				curl_easy_cleanup(ctx);
			}
		}
	
	private:
		static ssize_t writefunction(const char *p, size_t, size_t sz, void *ud) {
			auto &buf = *static_cast<std::string*>(ud);
			
			if (sz) {
				buf.append(static_cast<const char*>(p), sz);
			}
			
			return sz;
		}
		
		static int progressfunction(void *p, curl_off_t total, curl_off_t now, curl_off_t, curl_off_t) {
			auto &ro = *reinterpret_cast<request_opts*>(p);
			
			if (ro.progressfn) {
				ro.progressfn(total, now);
			}
			
			return 0;
		}
		
		CURL *ctx;
		std::unique_ptr<char[]> errbuf;
	};
}
