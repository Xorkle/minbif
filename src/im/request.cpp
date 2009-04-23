/*
 * Copyright(C) 2009 Romain Bignon
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "request.h"
#include "im.h"
#include "purple.h"
#include "irc/irc.h"
#include "irc/user.h"
#include "../util.h"

namespace im {


void RequestField::runCallback()
{
	callback(data, id);
}

Request::Request(PurpleRequestType _type, string _title, string _question)
	: title(_title),
	  question(_question),
	  type(_type)
{

}

void Request::addField(RequestField field)
{
	fields[field.getLabel()] = field;
}

RequestField Request::getField(const string& label) const
{
	map<string, RequestField>::const_iterator it = fields.find(label);
	if(it == fields.end())
		throw RequestFieldNotFound();

	return it->second;
}

void Request::display() const
{
	irc::IRC* irc = Purple::getIM()->getIRC();
	irc->privmsg(irc->getUser(), "New request: " + title);
	irc->privmsg(irc->getUser(), question);
	for(map<string, RequestField>::const_iterator it = fields.begin();
	    it != fields.end();
	    ++it)
	{
		irc->privmsg(irc->getUser(), "- " + it->first + ": " + it->second.getText());
	}
}

void Request::close()
{
	purple_request_close(type, this);
}

/* STATIC */

PurpleRequestUiOps Request::uiops =
{
        NULL,//finch_request_input,
        NULL,//finch_request_choice,
	Request::request_action,
        NULL,//finch_request_fields,
        NULL,//finch_request_file,
        NULL,//finch_close_request,
        NULL,//finch_request_folder,
        NULL,
        NULL,
        NULL,
        NULL
};

vector<Request*> Request::requests;

void Request::init()
{
	purple_request_set_ui_ops(&uiops);
}

void Request::answerRequest(const string& answer)
{
	irc::IRC* irc = Purple::getIM()->getIRC();
	if(requests.empty())
	{
		irc->privmsg(irc->getUser(), "No active question");
		return;
	}

	Request* request = requests.front();
	RequestField field;
	try
	{
		field = request->getField(answer);
	}
	catch(RequestFieldNotFound &e)
	{
		irc->privmsg(irc->getUser(), "ERROR: Answer '" + answer + "' is not valid.");
		request->display();
		return;
	}

	field.runCallback();
	request->close();
	delete request;
	requests.erase(requests.begin());
}

void* Request::request_action(const char *title, const char *primary,
			const char *secondary, int default_value,
			PurpleAccount *account, const char *who, PurpleConversation *conv,
			void *user_data, size_t actioncount,
			va_list actions)
{
	Request* request = new Request(PURPLE_REQUEST_ACTION, title, primary);
	for(size_t i = 0; i < actioncount; ++i)
	{
		const char *text = va_arg(actions, const char *);
		string tmp = text;
		PurpleRequestActionCb callback = va_arg(actions, PurpleRequestActionCb);

		request->addField(RequestField((int)i, strlower(stringtok(tmp, " ")), text, callback, user_data));
	}
	requests.push_back(request);
	if(requests.size() == 1)
		request->display();

	return requests.back();
}

}; /* namespace im */