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
#include "stdafx.h"
#include "DistributedClient.h"
#include <time.h>
#include <strEx.h>

#include <strEx.h>
#include <net/net.hpp>
#include <nscp/client/socket.hpp>
#include <zeromq/client.hpp>

#include <protobuf/plugin.pb.h>

#include <settings/client/settings_client.hpp>

namespace sh = nscapi::settings_helper;

/**
 * Default c-tor
 * @return 
 */
DistributedClient::DistributedClient() {}

/**
 * Default d-tor
 * @return 
 */
DistributedClient::~DistributedClient() {}

/**
 * Load (initiate) module.
 * Start the background collector thread and let it run until unloadModule() is called.
 * @return true
 */
bool DistributedClient::loadModule() {
	return false;
}

bool DistributedClient::loadModuleEx(std::wstring alias, NSCAPI::moduleLoadMode mode) {
	std::map<std::wstring,std::wstring> commands;

	std::wstring certificate;
	unsigned int timeout = 30, buffer_length = 1024;
	bool use_ssl = true;
	try {

		register_command(_T("query_dist"), _T("Submit a query to a remote host via NSCP"));
		register_command(_T("submit_dist"), _T("Submit a query to a remote host via NSCP"));
		register_command(_T("exec_dist"), _T("Execute remote command on a remote host via NSCP"));

		sh::settings_registry settings(get_settings_proxy());
		settings.set_alias(_T("distributed"), alias, _T("client"));

		target_path = settings.alias().get_settings_path(_T("targets"));

		settings.alias().add_path_to_settings()
			(_T("Distributed NSCP CLIENT SECTION"), _T("Section for NSCP active/passive check module."))

			(_T("handlers"), sh::fun_values_path(boost::bind(&DistributedClient::add_command, this, _1, _2)), 
			_T("CLIENT HANDLER SECTION"), _T(""))

			(_T("targets"), sh::fun_values_path(boost::bind(&DistributedClient::add_target, this, _1, _2)), 
			_T("REMOTE TARGET DEFINITIONS"), _T(""))
			;

		settings.alias().add_key_to_settings()
			(_T("channel"), sh::wstring_key(&channel_, _T("DNSCP")),
			_T("CHANNEL"), _T("The channel to listen to."))

			;

		settings.alias(_T("/targets/default")).add_key_to_settings()

			(_T("timeout"), sh::uint_key(&timeout, 30),
			_T("TIMEOUT"), _T("Timeout when reading/writing packets to/from sockets."))

			(_T("use ssl"), sh::bool_key(&use_ssl, true),
			_T("ENABLE SSL ENCRYPTION"), _T("This option controls if SSL should be enabled."))

			(_T("certificate"), sh::wpath_key(&certificate, _T("${certificate-path}/nrpe_dh_512.pem")),
			_T("SSL CERTIFICATE"), _T(""))

			(_T("payload length"),  sh::uint_key(&buffer_length, 1024),
			_T("PAYLOAD LENGTH"), _T("Length of payload to/from the NRPE agent. This is a hard specific value so you have to \"configure\" (read recompile) your NRPE agent to use the same value for it to work."))
			;

		settings.register_all();
		settings.notify();

		get_core()->registerSubmissionListener(get_id(), channel_);

		if (!targets.has_target(_T("default"))) {
			add_target(_T("default"), _T("default"));
			targets.rebuild();
		}
		nscapi::target_handler::optarget t = targets.find_target(_T("default"));
		if (t) {
			if (!t->has_option("certificate"))
				t->options[_T("certificate")] = certificate;
			if (!t->has_option("timeout"))
				t->options[_T("timeout")] = strEx::itos(timeout);
			if (!t->has_option("payload length"))
				t->options[_T("payload length")] = strEx::itos(buffer_length);
			if (!t->has_option("ssl"))
				t->options[_T("ssl")] = use_ssl?_T("true"):_T("false");
			targets.add(*t);
		} else {
			NSC_LOG_ERROR(_T("Default target not found!"));
		}

	} catch (nscapi::nscapi_exception &e) {
		NSC_LOG_ERROR_STD(_T("NSClient API exception: ") + utf8::to_unicode(e.what()));
		return false;
	} catch (std::exception &e) {
		NSC_LOG_ERROR_STD(_T("Exception caught: ") + utf8::to_unicode(e.what()));
		return false;
	} catch (...) {
		NSC_LOG_ERROR_STD(_T("Exception caught: <UNKNOWN EXCEPTION>"));
		return false;
	}
	return true;
}

//////////////////////////////////////////////////////////////////////////
// Settings helpers
//

void DistributedClient::add_target(std::wstring key, std::wstring arg) {
	try {
		targets.add(get_settings_proxy(), target_path , key, arg);
	} catch (...) {
		NSC_LOG_ERROR_STD(_T("Failed to add target: ") + key);
	}
}

void DistributedClient::add_command(std::wstring name, std::wstring args) {
	try {
		std::wstring key = commands.add_command(name, args);
		if (!key.empty())
			register_command(key.c_str(), _T("Custom command for: ") + name);
	} catch (boost::program_options::validation_error &e) {
		NSC_LOG_ERROR_STD(_T("Could not add command ") + name + _T(": ") + utf8::to_unicode(e.what()));
	} catch (...) {
		NSC_LOG_ERROR_STD(_T("Could not add command ") + name);
	}
}

/**
 * Unload (terminate) module.
 * Attempt to stop the background processing thread.
 * @return true if successfully, false if not (if not things might be bad)
 */
bool DistributedClient::unloadModule() {
	return true;
}

NSCAPI::nagiosReturn DistributedClient::handleCommand(const std::wstring &target, const std::wstring &command, std::list<std::wstring> &arguments, std::wstring &message, std::wstring &perf) {
	std::wstring cmd = client::command_line_parser::parse_command(command, _T("dist"));

	client::configuration config;
	setup(config);
	if (cmd == _T("query"))
		return client::command_line_parser::query(config, cmd, arguments, message, perf);
	if (cmd == _T("submit")) {
		boost::tuple<int,std::wstring> result = client::command_line_parser::simple_submit(config, cmd, arguments);
		message = result.get<1>();
		return result.get<0>();
	}
	if (cmd == _T("exec")) {
		return client::command_line_parser::exec(config, cmd, arguments, message);
	}
	return commands.exec_simple(config, target, command, arguments, message, perf);
}

int DistributedClient::commandLineExec(const std::wstring &command, std::list<std::wstring> &arguments, std::wstring &result) {
	std::wstring cmd = client::command_line_parser::parse_command(command, _T("nrpe"));
	if (!client::command_line_parser::is_command(cmd))
		return NSCAPI::returnIgnored;

	client::configuration config;
	setup(config);
	return client::command_line_parser::commandLineExec(config, cmd, arguments, result);
}

NSCAPI::nagiosReturn DistributedClient::handleRAWNotification(const wchar_t* channel, std::string request, std::string &response) {
	try {
		client::configuration config;
		setup(config);

		if (!client::command_line_parser::relay_submit(config, request, response)) {
			NSC_LOG_ERROR_STD(_T("Failed to submit message..."));
			return NSCAPI::hasFailed;
		}
		return NSCAPI::isSuccess;
	} catch (std::exception &e) {
		NSC_LOG_ERROR_STD(_T("Failed to send data: ") + utf8::to_unicode(e.what()));
		return NSCAPI::hasFailed;
	} catch (...) {
		NSC_LOG_ERROR_STD(_T("Failed to send data: UNKNOWN"));
		return NSCAPI::hasFailed;
	}
}

//////////////////////////////////////////////////////////////////////////
// Parser setup/Helpers
//

void DistributedClient::add_local_options(po::options_description &desc, client::configuration::data_type data) {
	desc.add_options()
		;
}

void DistributedClient::setup(client::configuration &config) {
	boost::shared_ptr<clp_handler_impl> handler = boost::shared_ptr<clp_handler_impl>(new clp_handler_impl(this));
	add_local_options(config.local, config.data);

	net::wurl url;
	url.protocol = _T("nrpe");
	url.port = 5666;
	nscapi::target_handler::optarget opt = targets.find_target(_T("default"));
	if (opt) {
		nscapi::target_handler::target t = *opt;
		url.host = t.host;
		if (t.has_option("port")) {
			try {
				url.port = strEx::stoi(t.options[_T("port")]);
			} catch (...) {}
}
		std::string keys[] = {"certificate", "timeout", "payload length", "ssl"};
		BOOST_FOREACH(std::string s, keys) {
			config.data->recipient.data[s] = utf8::cvt<std::string>(t.options[utf8::cvt<std::wstring>(s)]);
		}
	}
	config.data->recipient.id = "default";
	config.data->recipient.address = utf8::cvt<std::string>(url.to_string());
	config.data->host_self.id = "self";
	//config.data->host_self.host = hostname_;

	config.target_lookup = handler;
	config.handler = handler;
}

DistributedClient::connection_data DistributedClient::parse_header(const ::Plugin::Common_Header &header) {
	nscapi::functions::destination_container recipient;
	nscapi::functions::parse_destination(header, header.recipient_id(), recipient, true);
	return connection_data(recipient);
}

//////////////////////////////////////////////////////////////////////////
// Parser implementations
//


int DistributedClient::clp_handler_impl::query(client::configuration::data_type data, ::Plugin::Common_Header* header, const std::string &request, std::string &reply) {
	NSCAPI::nagiosReturn ret = NSCAPI::returnOK;
	try {

		Plugin::QueryRequestMessage request_message;
		request_message.ParseFromString(request);
		connection_data con = parse_header(*header);

		std::list<nscp::packet> chunks;
		chunks.push_back(nscp::factory::create_payload(nscp::data::command_request, request, 0));
		chunks = instance->send(con, chunks);
		BOOST_FOREACH(nscp::packet &packet, chunks) {
			if (nscp::checks::is_query_response(packet)) {
				reply = packet.payload;
			} else if (nscp::checks::is_error(packet)) {
				NSCPIPC::ErrorMessage message;
				message.ParseFromString(packet.payload);
				for (int i=0;i<message.error_size();i++) {
					NSC_LOG_ERROR_STD(_T("Error: ") + utf8::cvt<std::wstring>(message.error(i).message()));
				}
				ret = NSCAPI::returnUNKNOWN;
			} else {
				NSC_LOG_ERROR_STD(_T("Unsupported message type: ") + strEx::itos(packet.signature.payload_type));
				ret = NSCAPI::returnUNKNOWN;
			}
		}
		return ret;
	} catch (std::exception &e) {
		NSC_LOG_ERROR_STD(_T("Exception: ") + utf8::cvt<std::wstring>(e.what()));
		return NSCAPI::returnUNKNOWN;
	}
}

int DistributedClient::clp_handler_impl::submit(client::configuration::data_type data, ::Plugin::Common_Header* header, const std::string &request, std::string &reply) {
	std::list<std::string> result;
	std::wstring channel;
	try {

		Plugin::SubmitRequestMessage message;
		message.ParseFromString(request);
		connection_data con = parse_header(*header);
		channel = utf8::cvt<std::wstring>(message.channel());

		std::list<nscp::packet> chunks;
		chunks.push_back(nscp::factory::create_payload(nscp::data::command_response, request, 0));
		chunks = instance->send(con, chunks);
		BOOST_FOREACH(nscp::packet &chunk, chunks) {
			if (nscp::checks::is_query_response(chunk)) {
				result.push_back(chunk.payload);
			} else if (nscp::checks::is_error(chunk)) {
				NSCPIPC::ErrorMessage message;
				message.ParseFromString(chunk.payload);
				for (int i=0;i<message.error_size();i++) {
					result.push_back("Error: " + message.error(i).message());
				}
			} else {
				NSC_LOG_ERROR_STD(_T("Unsupported message type: ") + strEx::itos(chunk.signature.payload_type));
				result.push_back("Invalid payload");
			}
		}
		if (result.empty()) {
			std::wstring msg;
			BOOST_FOREACH(std::string &e, result) {
				msg += utf8::cvt<std::wstring>(e);
			}
			nscapi::functions::create_simple_submit_response(channel, _T(""), result.empty()?NSCAPI::isSuccess:NSCAPI::hasFailed, msg, reply);
		}

		return result.empty()?NSCAPI::isSuccess:NSCAPI::hasFailed;
	} catch (std::exception &e) {
		NSC_LOG_ERROR_STD(_T("Exception: ") + utf8::cvt<std::wstring>(e.what()));
		nscapi::functions::create_simple_submit_response(channel, _T(""), NSCAPI::hasFailed, utf8::cvt<std::wstring>(e.what()), reply);
		return NSCAPI::hasFailed;
	}
}

int DistributedClient::clp_handler_impl::exec(client::configuration::data_type data, ::Plugin::Common_Header* header, const std::string &request, std::string &reply) {
	int ret = NSCAPI::returnOK;
	try {
		Plugin::ExecuteRequestMessage request_message;
		request_message.ParseFromString(request);
		connection_data con = parse_header(*header);

		std::list<nscp::packet> chunks;
		chunks.push_back(nscp::factory::create_payload(nscp::data::exec_request, request, 0));
		chunks = instance->send(con, chunks);
		BOOST_FOREACH(nscp::packet &chunk, chunks) {
			if (nscp::checks::is_exec_response(chunk)) {
				reply = chunk.payload;
			} else if (nscp::checks::is_error(chunk)) {
				NSCPIPC::ErrorMessage message;
				message.ParseFromString(chunk.payload);
				for (int i=0;i<message.error_size();i++) {
					NSC_LOG_ERROR_STD(_T("Error: ") + utf8::cvt<std::wstring>(message.error(i).message()));
					ret = NSCAPI::returnUNKNOWN;
				}
			} else {
				NSC_LOG_ERROR_STD(_T("Unsupported message type: ") + strEx::itos(chunk.signature.payload_type));
				ret = NSCAPI::returnUNKNOWN;
			}
		}
		return ret;
	} catch (std::exception &e) {
		NSC_LOG_ERROR_STD(_T("Exception: ") + utf8::cvt<std::wstring>(e.what()));
		return NSCAPI::returnUNKNOWN;
	}
}

//////////////////////////////////////////////////////////////////////////
// Protocol implementations
//

std::list<nscp::packet> DistributedClient::send(connection_data &data, std::list<nscp::packet> &chunks) {
	std::list<nscp::packet> tmp, result;
	// @todo: fix this (take from header)
	/*
	std::wstring host = generic_data->host;
	if (host.empty() && !generic_data->target.empty()) {
		nscapi::target_handler::optarget t = targets.find_target(generic_data->target);
		if (t)
			host = (*t).host;
	}
	*/
	return send_nossl(data.address, data.timeout, chunks);
}

std::list<nscp::packet> DistributedClient::send_nossl(std::string host, int timeout, const std::list<nscp::packet> &chunks) {
	zmq::context_t context(1);
	zeromq::zeromq_client_handshake_reader handshaker("0987654321", nscp::factory::create_message_envelope_request(0));
	zeromq::zeromq_client client(zeromq::zeromq_client::connection_info(host, timeout), &handshaker, &context);
	client.connect();
	std::list<nscp::packet> responses;
	NSC_DEBUG_MSG_STD(_T("Sending data: ") + to_wstring(chunks.size()));
	BOOST_FOREACH(const nscp::packet packet, chunks) {
		zeromq::zeromq_client_message_reader reader(&handshaker, packet);
		if (client.send(&reader)) {
			NSC_DEBUG_MSG(_T("Got response: ") + to_wstring(reader.response.signature.payload_type));
			responses.push_back(reader.response);
		} else {
			NSC_LOG_ERROR(_T("Failed to read response!"));
			responses.push_back(nscp::factory::create_error(_T("Failed to send data to server.")));
		}

	}
	return responses;
}

NSC_WRAP_DLL();
NSC_WRAPPERS_MAIN_DEF(DistributedClient);
NSC_WRAPPERS_IGNORE_MSG_DEF();
NSC_WRAPPERS_HANDLE_CMD_DEF();
NSC_WRAPPERS_CLI_DEF();
NSC_WRAPPERS_HANDLE_NOTIFICATION_DEF();

