#pragma once

#include <string>
#include <string_view>

namespace ugconv {
	struct response {
		long code = 0;
		std::string message;
		std::string body;
	};
	
	struct request_opts {
		std::string_view referer;
		std::string_view user_agent;
		std::string_view cookies;
	};
	
	struct requester {
		virtual response get(std::string_view url, const request_opts&) = 0;
		
		virtual ~requester() = default;
	};
}
