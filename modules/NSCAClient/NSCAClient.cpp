/**************************************************************************
*   Copyright (C) 2004-2007 by Michael Medin <michael@medin.name>         *
*                                                                         *
*   This code is part of NSClient++ - http://trac.nakednuns.org/nscp      *
*                                                                         *
*   This program is free software; you can redistribute it and/or modify  *
*   it under the terms of the GNU General Public License as published by  *
*   the Free Software Foundation; either version 2 of the License, or     *
*   (at your option) any later version.                                   *
*                                                                         *
*   This program is distributed in the hope that it will be useful,       *
*   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
*   GNU General Public License for more details.                          *
*                                                                         *
*   You should have received a copy of the GNU General Public License     *
*   along with this program; if not, write to the                         *
*   Free Software Foundation, Inc.,                                       *
*   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
***************************************************************************/
#include "NSCAClient.h"

#include <utils.h>
#include <strEx.h>

#include <nscapi/nscapi_settings_helper.hpp>
#include <nscapi/nscapi_protobuf_functions.hpp>
#include <nscapi/nscapi_core_helper.hpp>

#include <boost/make_shared.hpp>

#include "nsca_client.hpp"
#include "nsca_handler.hpp"

/**
 * Default c-tor
 * @return 
 */
NSCAClient::NSCAClient() : client_("nsca", boost::make_shared<nsca_client::nsca_client_handler>(), boost::make_shared<nsca_handler::options_reader_impl>()) {}

/**
 * Default d-tor
 * @return 
 */
NSCAClient::~NSCAClient() {}

bool NSCAClient::loadModuleEx(std::string alias, NSCAPI::moduleLoadMode) {

	try {

		sh::settings_registry settings(get_settings_proxy());
		settings.set_alias("NSCA", alias, "client");
		target_path = settings.alias().get_settings_path("targets");

		settings.alias().add_path_to_settings()
			("NSCA CLIENT SECTION", "Section for NSCA passive check module.")

			("handlers", sh::fun_values_path(boost::bind(&NSCAClient::add_command, this, _1, _2)), 
			"CLIENT HANDLER SECTION", "",
			"CLIENT HANDLER", "For more configuration options add a dedicated section")

			("targets", sh::fun_values_path(boost::bind(&NSCAClient::add_target, this, _1, _2)), 
			"REMOTE TARGET DEFINITIONS", "",
			"TARGET", "For more configuration options add a dedicated section")
			;

		settings.alias().add_key_to_settings()
			("hostname", sh::string_key(&hostname_, "auto"),
			"HOSTNAME", "The host name of the monitored computer.\nSet this to auto (default) to use the windows name of the computer.\n\n"
			"auto\tHostname\n"
			"${host}\tHostname\n"
			"${host_lc}\nHostname in lowercase\n"
			"${host_uc}\tHostname in uppercase\n"
			"${domain}\tDomainname\n"
			"${domain_lc}\tDomainname in lowercase\n"
			"${domain_uc}\tDomainname in uppercase\n"
			)

			("encoding", sh::string_key(&encoding_, ""),
			"NSCA DATA ENCODING", "", true)

			("channel", sh::string_key(&channel_, "NSCA"),
			"CHANNEL", "The channel to listen to.")
			;

		settings.register_all();
		settings.notify();

		client_.finalize(get_settings_proxy());


		nscapi::core_helper core(get_core(), get_id());
		core.register_channel(channel_);

		if (hostname_ == "auto") {
			hostname_ = boost::asio::ip::host_name();
		} else if (hostname_ == "auto-lc") {
			hostname_ = boost::asio::ip::host_name();
			std::transform(hostname_.begin(), hostname_.end(), hostname_.begin(), ::tolower);
		} else if (hostname_ == "auto-uc") {
			hostname_ = boost::asio::ip::host_name();
			std::transform(hostname_.begin(), hostname_.end(), hostname_.begin(), ::toupper);
		} else {
			strEx::s::token dn = strEx::s::getToken(boost::asio::ip::host_name(), '.');

			try {
				boost::asio::io_service svc;
				boost::asio::ip::tcp::resolver resolver (svc);
				boost::asio::ip::tcp::resolver::query query (boost::asio::ip::host_name(), "");
				boost::asio::ip::tcp::resolver::iterator iter = resolver.resolve (query), end;

				std::string s;
				while (iter != end) {
					s += iter->host_name();
					s += " - ";
					s += iter->endpoint().address().to_string();
					iter++;
				}
			} catch (const std::exception& e) {
				NSC_LOG_ERROR_EXR("Failed to resolve: ", e);
			}
			strEx::replace(hostname_, "${host}", dn.first);
			strEx::replace(hostname_, "${domain}", dn.second);
			std::transform(dn.first.begin(), dn.first.end(), dn.first.begin(), ::toupper);
			std::transform(dn.second.begin(), dn.second.end(), dn.second.begin(), ::toupper);
			strEx::replace(hostname_, "${host_uc}", dn.first);
			strEx::replace(hostname_, "${domain_uc}", dn.second);
			std::transform(dn.first.begin(), dn.first.end(), dn.first.begin(), ::tolower);
			std::transform(dn.second.begin(), dn.second.end(), dn.second.begin(), ::tolower);
			strEx::replace(hostname_, "${host_lc}", dn.first);
			strEx::replace(hostname_, "${domain_lc}", dn.second);
		}
	} catch (nscapi::nscapi_exception &e) {
		NSC_LOG_ERROR_EXR("Failed to load NSCAClient", e);
		return false;
	} catch (std::exception &e) {
		NSC_LOG_ERROR_EXR("Failed to send", e);
		return false;
	} catch (...) {
		NSC_LOG_ERROR_EX("Failed to send");
		return false;
	}
	return true;
}

std::string get_command(std::string alias, std::string command = "") {
	if (!alias.empty())
		return alias; 
	if (!command.empty())
		return command; 
	return "host_check";
}

//////////////////////////////////////////////////////////////////////////
// Settings helpers
//

void NSCAClient::add_target(std::string key, std::string arg) {
	try {
		client_.add_target(get_settings_proxy(), key, arg);
	} catch (const std::exception &e) {
		NSC_LOG_ERROR_EXR("Failed to add target: " + key, e);
	} catch (...) {
		NSC_LOG_ERROR_EX("Failed to add target: " + key);
	}
}

void NSCAClient::add_command(std::string key, std::string arg) {
	try {
		nscapi::core_helper core(get_core(), get_id());
		std::string k = client_.add_command(key, arg);
		if (!k.empty())
			core.register_command(k.c_str(), "NSCA relay for: " + key);
	} catch (const std::exception &e) {
		NSC_LOG_ERROR_EXR("Failed to add command: " + key, e);
	} catch (...) {
		NSC_LOG_ERROR_EX("Failed to add command: " + key);
	}
}

/**
 * Unload (terminate) module.
 * Attempt to stop the background processing thread.
 * @return true if successfully, false if not (if not things might be bad)
 */
bool NSCAClient::unloadModule() {
	client_.clear();
	return true;
}

void NSCAClient::query_fallback(const Plugin::QueryRequestMessage &request_message, Plugin::QueryResponseMessage &response_message) {
	client_.do_query(request_message, response_message);
}

bool NSCAClient::commandLineExec(const Plugin::ExecuteRequestMessage &request, Plugin::ExecuteResponseMessage &response) {
	return client_.do_exec(request, response);
}

void NSCAClient::handleNotification(const std::string &, const Plugin::SubmitRequestMessage &request_message, Plugin::SubmitResponseMessage *response_message) {
	client_.do_submit(request_message, *response_message);
}

