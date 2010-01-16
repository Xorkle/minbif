/*
 * Minbif - IRC instant messaging gateway
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

#include <errno.h>
#include <string.h>

#include "request.h"
#include "buddy.h"
#include "im.h"
#include "purple.h"
#include "irc/irc.h"
#include "irc/user.h"
#include "core/util.h"
#include "core/log.h"
#include "core/config.h"

namespace im {

class RequestNick : public irc::Nick
{
public:

	RequestNick(irc::Server* server, string nickname, string identname, string hostname, string realname="")
		: irc::Nick(server, nickname, identname, hostname, realname)
	{}

	void request_error(string msg)
	{
		irc::IRC* irc = Purple::getIM()->getIRC();
		privmsg(irc->getUser(), msg);
	}

	void send(irc::Message m)
	{
		if(m.getCommand() != MSG_PRIVMSG || m.getReceiver() != this)
			return;

		string answer = m.getArg(0);
		irc::IRC* irc = Purple::getIM()->getIRC();
		Request* request = Request::getFirstRequest();
		if(!request)
		{
			privmsg(irc->getUser(), "No active question");
			return;
		}

		try
		{
			request->process(answer);
		}
		catch(RequestFieldNotFound &e)
		{
			privmsg(irc->getUser(), "ERROR: Answer '" + answer + "' is not valid.");
			request->display();
			return;
		}

		request->close();
	}
};

template<typename _CbT, typename _CbData2T>
class RequestFieldTmplt : public RequestField
{
	int id;
	string label;
	string text;
	_CbT callback;
	void* data;
	_CbData2T data2;

public:

	RequestFieldTmplt() : id(-1), callback(NULL), data(NULL) {}

	RequestFieldTmplt(int _id, string _label, string _text, _CbT _callback, void* _data, _CbData2T _data2)
		: id(_id),
		  label(_label),
		  text(_text),
		  callback(_callback),
		  data(_data),
		  data2(_data2)
	{}

	virtual ~RequestFieldTmplt() {}

	int getID() const { return id; }
	void setLabel(const string& l) { label = l; }
	string getLabel() const { return label; }
	string getText() const { return text; }
	void runCallback()
	{
		if (callback)
			callback(data, data2);
	}
};

template<typename _CbT>
class RequestFieldAction : public RequestFieldTmplt<_CbT, int>
{
public:
	RequestFieldAction(int id, string label, string txt, _CbT callback, void* data)
		: RequestFieldTmplt<_CbT, int>(id, label, txt, callback, data, id)
	{}
};

Request::Request(PurpleRequestType _type, const string& _title, const string& _question)
	: title(_title),
	  question(_question),
	  type(_type)
{

}

RequestFieldList::~RequestFieldList()
{
	map<string, RequestField*>::iterator it;
	for(it = fields.begin(); it != fields.end(); ++it)
		delete it->second;
}

void RequestFieldList::addField(RequestField* field)
{
	while(fields.find(field->getLabel()) != fields.end())
		field->setLabel(field->getLabel() + "_");

	fields[field->getLabel()] = field;
}

RequestField* RequestFieldList::getField(const string& label) const
{
	map<string, RequestField*>::const_iterator it = fields.find(label);
	if(it == fields.end())
		throw RequestFieldNotFound();

	return it->second;
}

void RequestFieldList::process(const string& answer) const
{
	RequestField* field = getField(answer);
	field->runCallback();
}

void RequestFieldList::display() const
{
	irc::IRC* irc = Purple::getIM()->getIRC();
	nick->privmsg(irc->getUser(), "New request: " + title);
	nick->privmsg(irc->getUser(), question);
	for(map<string, RequestField*>::const_iterator it = fields.begin();
	    it != fields.end();
	    ++it)
	{
		nick->privmsg(irc->getUser(), "- " + it->first + ": " + it->second->getText());
	}
}

void RequestInput::process(const string& answer) const
{
	callback(user_data, answer.c_str());
}

void RequestInput::display() const
{
	irc::IRC* irc = Purple::getIM()->getIRC();
	nick->privmsg(irc->getUser(), "New request: " + title);
	nick->privmsg(irc->getUser(), question);
	nick->privmsg(irc->getUser(), "Default value is: " + default_value);
	nick->privmsg(irc->getUser(), "Type answer:");
}

void Request::close()
{
	purple_request_close(type, this);
}

/* STATIC */

PurpleRequestUiOps Request::uiops =
{
	Request::request_input,
        Request::request_choice,
        Request::request_action,
        Request::request_fields,
        Request::request_file,
        Request::request_close,
        NULL,//finch_request_folder,
        NULL,
        NULL,
        NULL,
        NULL
};

PurpleNotifyUiOps Request::notify_ops =
{
	Request::notify_message,
        NULL,//finch_notify_email,
        NULL,//finch_notify_emails,
        Request::notify_formatted,
        NULL,//finch_notify_searchresults,
        NULL,//finch_notify_sr_new_rows,
        Request::notify_userinfo,
        NULL,//finch_notify_uri,
        NULL,//finch_close_notify,       /* The rest of the notify-uiops return a GntWidget.
             //                             These widgets should be destroyed from here. */
        NULL,
        NULL,
        NULL,
        NULL

};

vector<Request*> Request::requests;
RequestNick* Request::nick = NULL;

void Request::init()
{
	purple_request_set_ui_ops(&uiops);
	purple_notify_set_ui_ops(&notify_ops);

	irc::IRC* irc = Purple::getIM()->getIRC();
	nick = new RequestNick(irc, "request", "request", irc->getServerName(), "Eric Cartman");
	irc->addNick(nick);
}

void Request::uninit()
{
	purple_request_set_ui_ops(NULL);
	purple_notify_set_ui_ops(NULL);
}

Request* Request::getFirstRequest()
{
	return requests.empty() ? NULL : requests.front();
}

void* Request::notify_message(PurpleNotifyMsgType type, const char *title,
				const char *primary, const char *secondary)
{
	irc::IRC* irc = Purple::getIM()->getIRC();

	bool titled = false;
	const char** texts[] = {&title, &primary, &secondary, NULL};
	for(const char*** ptr = texts; *ptr != NULL; ++ptr)
	{
		if(!**ptr || !***ptr) continue;

		string txt = **ptr, line;
		if(!titled)
		{
			titled = true;
			if(txt.find("\n") == string::npos)
			{
				nick->privmsg(irc->getUser(), ":: " + txt + " ::");
				continue;
			}
		}
		while((line = stringtok(txt, "\r\n")).empty() == false)
			nick->privmsg(irc->getUser(), line);
	}

	return NULL;
}

void* Request::notify_formatted(const char *title, const char *primary, const char *secondary,
			      const char *text)
{
	irc::IRC* irc = Purple::getIM()->getIRC();

	char* text_stripped = purple_markup_strip_html(text);

	bool titled = false;
	const char** texts[] = {&title, &primary, &secondary, (const char**) &text_stripped, NULL};
	for(const char*** ptr = texts; *ptr != NULL; ++ptr)
	{
		if(!**ptr || !***ptr) continue;

		string txt = **ptr, line;
		if(!titled)
		{
			titled = true;
			if(txt.find("\n") == string::npos)
			{
				nick->privmsg(irc->getUser(), ":: " + txt + " ::");
				continue;
			}
		}
		while((line = stringtok(txt, "\r\n")).empty() == false)
			nick->privmsg(irc->getUser(), line);
	}

	g_free(text_stripped);

	return NULL;
}

void* Request::notify_userinfo(PurpleConnection *gc, const char *who, PurpleNotifyUserInfo *user_info)
{
	GList* l = purple_notify_user_info_get_entries(user_info);
	irc::IRC* irc = Purple::getIM()->getIRC();
	irc::Nick* user = irc->getUser();
	PurpleBuddy* buddy = purple_find_buddy(gc->account, who);
	Buddy b(buddy);

	if(!b.isValid())
	{
		b_log[W_ERR] << "Received WHOIS replies about unknown buddy " << who;
		return NULL;
	}

	for (; l != NULL; l = l->next)
	{
		PurpleNotifyUserInfoEntry *user_info_entry = (PurpleNotifyUserInfoEntry*)l->data;
		//PurpleNotifyUserInfoEntryType type = purple_notify_user_info_entry_get_type(user_info_entry);
		const char *label = purple_notify_user_info_entry_get_label(user_info_entry);
		const char *_value = purple_notify_user_info_entry_get_value(user_info_entry);

		if(!label) continue;

		string text;
		if(_value)
		{
			char* value = purple_markup_strip_html(_value);
			text = string(label) + ": " + value;
			g_free(value);
		}
		else
			text = string(":: ") + label + " ::";

		string line;
		while((line = stringtok(text, "\r\n")).empty() == false)
			user->send(irc::Message(RPL_WHOISACTUALLY).setSender(irc)
						       .setReceiver(user)
						       .addArg(b.getAlias())
						       .addArg(line));
	}
	user->send(irc::Message(RPL_ENDOFWHOIS).setSender(irc)
						  .setReceiver(user)
						  .addArg(b.getAlias())
						  .addArg("End of /WHOIS list"));

	return NULL;
}

void Request::request_close(PurpleRequestType type, void *ui_handle)
{
	irc::IRC* irc = Purple::getIM()->getIRC();
	for(vector<Request*>::iterator it = requests.begin(); it != requests.end(); ++it)
	{
		if(*it != ui_handle)
			continue;

		bool first = (it == requests.begin());
		delete *it;
		requests.erase(it);

		if(first)
		{
			nick->privmsg(irc->getUser(), "Request closed.");
			if(!requests.empty())
				requests.front()->display();
		}
		return;
	}
}

void* Request::request_input(const char *title, const char *primary,
			const char *secondary, const char *default_value,
			gboolean multiline, gboolean masked, gchar *hint,
			const char *ok_text, GCallback ok_cb,
			const char *cancel_text, GCallback cancel_cb,
			PurpleAccount *account, const char *who, PurpleConversation *conv,
			void *user_data)
{
	RequestInput* request = new RequestInput(PURPLE_REQUEST_INPUT, title ? title : "", primary ? primary : "", default_value ? default_value : "", (PurpleRequestInputCb)ok_cb, user_data);
	requests.push_back(request);
	if(requests.size() == 1)
		request->display();

	return requests.back();
}

void* Request::request_action(const char *title, const char *primary,
			const char *secondary, int default_value,
			PurpleAccount *account, const char *who, PurpleConversation *conv,
			void *user_data, size_t actioncount,
			va_list actions)
{
	RequestFieldList* request = new RequestFieldList(PURPLE_REQUEST_ACTION, title ? title : "", primary ? primary : "");
	for(size_t i = 0; i < actioncount; ++i)
	{
		const char *text = va_arg(actions, const char *);
		string tmp = text;
		PurpleRequestActionCb callback = va_arg(actions, PurpleRequestActionCb);

		request->addField(new RequestFieldAction<PurpleRequestActionCb>((int)i, strlower(stringtok(tmp, "_ ")), text, callback, user_data));
	}
	requests.push_back(request);
	if(requests.size() == 1)
		request->display();

	return requests.back();
}

void* Request::request_choice(const char *title, const char *primary,
			const char *secondary, int default_value,
			const char *ok_text, GCallback ok_cb,
			const char *cancel_text, GCallback cancel_cb,
			PurpleAccount *account, const char *who, PurpleConversation *conv,
			void *user_data, va_list choices)
{
	RequestFieldList* request = new RequestFieldList(PURPLE_REQUEST_CHOICE, title ? title : "", primary ? primary : "");
	const char* text;

	while ((text = va_arg(choices, const char *)))
	{
		int val = va_arg(choices, int);
		string tmp;

		request->addField(new RequestFieldAction<PurpleRequestChoiceCb>(val, strlower(stringtok(tmp, "_ ")), text, (PurpleRequestChoiceCb)ok_cb, user_data));
	}
	request->addField(new RequestFieldAction<PurpleRequestChoiceCb>(0, "cancel", "Cancel", (PurpleRequestChoiceCb)cancel_cb, user_data));

	requests.push_back(request);
	if(requests.size() == 1)
		request->display();

	return requests.back();
}

void* Request::request_fields(const char *title, const char *primary,
		const char *secondary, PurpleRequestFields *fields,
		const char *ok, GCallback ok_cb,
		const char *cancel, GCallback cancel_cb,
		PurpleAccount *account, const char *who, PurpleConversation *conv,
		void *userdata)
{
	RequestFieldList* request = new RequestFieldList(PURPLE_REQUEST_FIELDS, title ? title : "", primary ? primary : "");
	request->addField(new RequestFieldTmplt<PurpleRequestFieldsCb,PurpleRequestFields*>(0, "ok", ok, (PurpleRequestFieldsCb)ok_cb, userdata, fields));
	request->addField(new RequestFieldTmplt<PurpleRequestFieldsCb,PurpleRequestFields*>(1, "cancel", cancel, (PurpleRequestFieldsCb)cancel_cb, userdata, fields));

	requests.push_back(request);
	if(requests.size() == 1)
		request->display();

	return requests.back();
}

void* Request::request_file(const char *title, const char *filename,
			  gboolean savedialog,
			  GCallback ok_cb, GCallback cancel_cb,
			  PurpleAccount *account, const char *who, PurpleConversation *conv,
			  void *user_data)
{
	if(conf.GetSection("file_transfers")->GetItem("enabled")->Boolean() == false)
	{
		b_log[W_ERR] << "File transfers are disabled on this server.";
		((PurpleRequestFileCb)cancel_cb)(user_data, NULL);
		return NULL;
	}

	if(savedialog)
	{
		/* This is probably (I hope) a request to know the path where
		 * to save a file that someone sends to me.
		 *
		 * Here we try to put it in /var/lib/minbif/romain/downloads/,
		 * and create directory if it doesn't exist.
		 *
		 * Important: the file returned MUST be writtable! If libpurple
		 *            tries to overload a unwrittable file, it'll write
		 *            an error, but also recall this function, and it
		 *            can produce an unfinite loop.
		 */
		string path = purple_user_dir();
		path += "/downloads/"; // XXX do not hardcode this.

		if(!check_write_file(path, filename))
		{
			b_log[W_ERR] << "Unable to write into the download directory '" << path << "': " << strerror(errno);
			((PurpleRequestFileCb)cancel_cb)(user_data, NULL);
			return NULL;
		}
		path += filename;

		((PurpleRequestFileCb)ok_cb)(user_data, path.c_str());
	}
	else
	{
		/* TODO Implement when libpurple requests to open a file. */
		nick->request_error("Warning: something tries to ask you to open a filename (" + string(title ? title : "") + "). But it is not yet implemented.");
		((PurpleRequestFileCb)cancel_cb)(user_data, NULL);
	}

	return NULL;
}

}; /* namespace im */
