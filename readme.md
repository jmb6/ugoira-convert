# ugoira-convert

This is a command-line program and a C++ header-only library for downloading and converting Ugoira animations from Pixiv.

It currently supports the following features:

- Downloading and converting from pixiv by artwork ID or URL.
- Converting to WebM.
- Converting to Gif.
- Converting from local PixivUtil2 .ugoira file.
- Converting from local zip + json file.

# Dependencies

`unzip` and `ffmpeg` must be installed on the system in order to work.

nlohmann-json and libcurl are required to build the program and use the library, although libcurl can be explicitly disabled when using the library, see the library section.

# Building the command-line program

Simply run

	make

Then to install,

	sudo make install

By default it is installed to `/usr/local/bin`. You can change this by setting the `INSTALLDIR` environment variable.

# Command-line program example usage

**Note:** If you want to download R-18 works, see the section below this one first!

Download a work by URL and convert to webm:

	ugoira-convert https://www.pixiv.net/en/artworks/<ID>

By URL but convert to gif:

	ugoira-convert https://www.pixiv.net/en/artworks/<ID> -fmt gif

By ID:

	ugoira-convert -id <ID>

From local .ugoira file:

	ugoira-convert -ugoira <PATH>

Zero web requests will be made to Pixiv in this case.

From local ugoira_meta.json file:

	ugoira-convert -meta <PATH>

ugoira-convert will fetch the zip file containing the frames from Pixiv.

If you have the zip file locally, then you can pass it in like this:

	ugoira-convert -meta <PATH> -zip <PATH>

Zero web requests will be made to Pixiv in this case.

Download by URL and save with a custom filename:

	ugoira-convert https://www.pixiv.net/en/artworks/<ID> some_filename

Download by URL and output into a given directory

	ugoira-convert https://www.pixiv.net/en/artworks/<ID> some_directory

In the above two cases, ugoira-convert simply checks if the path refers to a directory or not to determine if we should interpret it as a filename or an output directory.

# Downloading R-18 works

Pixiv requires you to be signed in to gain access to R-18 works.

To do this, sign into your Pixiv account and copy-paste the value of the PHPSESSID cookie. Then when downloading, pass this into ugoira-convert using the `-s` flag:

	ugoira-convert <URL> -s <SESSION-ID>

You can also set the `UGCONV_SESSION_ID` environment variable so you don't have to keep manually passing it in with `-s`.

**Note: Pixiv invalidates and generates a new PHPSESSID every time you sign in or out of your account. If you don't want to have to keep updating the session ID for ugoira-convert, simply never sign out of your Pixiv account.**

If you know a better way, let me know.

# Usage

	ugoira-convert [OPTIONS...] [URL] [FILENAME|DIRECTORY]

Options:

- `-u <STRING>`: Set user-agent. This should be a normal browser useragent. By default it is `Mozilla/5.0 (X11; Linux x86_64; rv:91.0) Gecko/20100101 Firefox/91.0`. Default libcurl user agent is blocked by Pixiv.
- `-s <STRING>`: Set PHPSESSID cookie. Used for user authentication. (See R-18 works section).
- `-fmt <STRING>`: Set the output format to convert to. Valid values are `webm` and `gif`.
- `-meta <PATH>`: Path to an ugoira_meta.json file. If this is provided ugoira-convert will use this file directly instead of fetching it from Pixiv. The zip file containing the actual frames will still be fetched from Pixiv though unless `-zip` is also given.
- `-zip <PATH>`: Path to a zip file containing ugoira frames. This requires `-meta` to also be passed. Tells ugoira-convert to use this zip file instead of downloading it from Pixiv.
- `-id <ID>`: Artwork ID to download. This is simply an alternative to supplying the whole URL. If this option is supplied then there is no `[URL]` parameter.
- `-q`: Be quiet.
- `-v`: Print all shell commands run.

# Header-only library

**Requirements:** A C++20 compiler, nlohmann-json, libcurl (if libcurl isn't disabled).

The main header to include is `<ugconv/ugconv.hpp>` which is located in the `include/` directory.

If you don't wish to link against libcurl, then you can define `UGCONV_NO_CURL` before including `ugconv.hpp`, in which case ugoira-convert will not be able to make web requests. However, you can still implement a custom requester to enable ugoira-convert to make requests through your own code. (See further below).

Otherwise, you'll need to link against libcurl.

The main context object is `ugconv::context`, this is the object you'll be carrying out all conversion operations through.

Basic example usage:

	#include <iostream>
	#include <ugconv/ugconv.hpp>

	int main() {
		ugconv::context ctx;
		
		if (auto r = ctx.set_post("https://www.pixiv.net/en/artworks/92197851"); !r) {
			std::cout << r.message << '\n';
			return 1;
		}
		
		ctx.set_session_id("asdf");
		
		if (auto r = ctx.convert("out.webm", ugconv::FMT_WEBM); !r) {
			std::cout << r.message << '\n';
			return 1;
		}
	}

When `ugconv::context::convert` returns, the ID/URL, ugoira, meta, and zip parameters are cleared.

For further usage, read the public definitions, functions, and methods in `ugconv.hpp`.

The `context` object is **not** thread-safe. If you wish to run multiple download/conversion jobs in parallel, you must use multiple context objects.

`context` objects are light-weight to create, so creating them on-demand is also feasible.

# Making a custom requester

If you wish to handle web requests yourself, you can do so by including `ugconv/request.hpp` and deriving from `ugconv::requester`.

Once you have implemented your custom requester (read `request.hpp` for the required methods to override and definitions you need), then instantiate it and pass it into the `context` constructor like this:

	my_requester req;
	ugconv::context ctx{req};

(again, read `ugconv.hpp` for the specific constructor signature.)

All web requests coming from that context will go through your customer requester. The requester object must stay live for the lifetime of the context object.
