
#include "ga_buddy.h"


GayAttitudeBuddy *ga_get_gabuddy_from_buddy(PurpleBuddy *buddy, gboolean create)
{
	GayAttitudeBuddy *gabuddy;

	if (!buddy)
		return NULL;

	gabuddy = buddy->proto_data;
	if (!gabuddy && create)
	{
		gabuddy = g_new0(GayAttitudeBuddy, TRUE);
		gabuddy->buddy = buddy;
		gabuddy->ref_id = NULL;
	}

	return gabuddy;
}

GayAttitudeBuddy *ga_find_gabuddy(GayAttitudeAccount *gaa, const gchar *buddyname)
{
	PurpleBuddy *buddy;
	GayAttitudeBuddy *gabuddy;

	buddy = purple_find_buddy(gaa->account, buddyname);
	if (!buddy)
		return NULL;

	gabuddy = ga_get_gabuddy_from_buddy(buddy, TRUE);

	return gabuddy;
}

GayAttitudeBuddy *ga_gabuddy_new(GayAttitudeAccount *gaa, const gchar *buddyname)
{
	PurpleBuddy *buddy;

	buddy = purple_buddy_new(gaa->account, buddyname, NULL);
	return ga_get_gabuddy_from_buddy(buddy, TRUE);
}

void ga_gabuddy_free(GayAttitudeBuddy *gabuddy)
{
	if (!gabuddy)
		return;

	g_free(gabuddy);
}

static void ga_parse_contact_info(HttpHandler* handler, gchar* response, gsize len, gpointer userdata)
{
	htmlDocPtr doc;
	xmlXPathContextPtr xpathCtx;
	xmlXPathObjectPtr xpathObj;
	GayAttitudeAccount *gaa = handler->data;
	GayAttitudeBuddyInfoRequest *request = userdata;

	doc = htmlReadMemory(response, len, "gayattitude.xml", NULL, 0);
	if (doc == NULL)
	{
		purple_debug(PURPLE_DEBUG_ERROR, "gayattitude", "Unable to parse response (XML Parsing).\n");
		return;
	}

	/* Create xpath evaluation context */
	xpathCtx = xmlXPathNewContext(doc);
	if(xpathCtx == NULL)
	{
		purple_debug(PURPLE_DEBUG_ERROR, "gayattitude", "Unable to parse response (XPath context init).\n");
		xmlFreeDoc(doc);
		return;
	}

	xmlNode *info_node;

	/* Search internal Ref ID */
	if (!request->gabuddy->ref_id)
	{
		xpathObj = xmlXPathEvalExpression((xmlChar*) "//input[@type='hidden' and @name='ref_id']", xpathCtx);
		if(xpathObj == NULL)
		{
			purple_debug(PURPLE_DEBUG_ERROR, "gayattitude", "Unable to parse response (XPath evaluation).\n");
			xmlXPathFreeContext(xpathCtx);
			xmlFreeDoc(doc);
			return;
		}
		if (!xmlXPathNodeSetIsEmpty(xpathObj->nodesetval))
		{
			info_node = xpathObj->nodesetval->nodeTab[0];
			request->gabuddy->ref_id  = (gchar*) xmlGetProp(info_node, (xmlChar*) "value");
		}
		xmlXPathFreeObject(xpathObj);
	}

	if (request->advertise)
	{
		PurpleNotifyUserInfo *user_info = purple_notify_user_info_new();
		int i;
		GString *str = NULL;

		/* Search short description */
		xpathCtx->node = doc->parent;
		xpathObj = xmlXPathEvalExpression((xmlChar*) "//div[@id='PORTRAITHEADER2']/p/text()", xpathCtx);
		if(xpathObj == NULL)
		{
			purple_debug(PURPLE_DEBUG_ERROR, "gayattitude", "Unable to parse response (XPath evaluation).\n");
			xmlXPathFreeContext(xpathCtx);
			xmlFreeDoc(doc);
			return;
		}
		if (!xmlXPathNodeSetIsEmpty(xpathObj->nodesetval))
		{
			info_node = xpathObj->nodesetval->nodeTab[0];
			purple_notify_user_info_add_pair(user_info, "Short Description", (gchar*) info_node->content);
		}
		xmlXPathFreeObject(xpathObj);

		/* Search user research */
		xpathCtx->node = doc->parent;
		xpathObj = xmlXPathEvalExpression((xmlChar*) "//div[@id='bloc_recherche']/p/text()", xpathCtx);
		if(xpathObj == NULL)
		{
			purple_debug(PURPLE_DEBUG_ERROR, "gayattitude", "Unable to parse response (XPath evaluation).\n");
			xmlXPathFreeContext(xpathCtx);
			xmlFreeDoc(doc);
			return;
		}
		if (!xmlXPathNodeSetIsEmpty(xpathObj->nodesetval))
		{
			for(i = 0; i < xpathObj->nodesetval->nodeNr; i++)
			{
				info_node = xpathObj->nodesetval->nodeTab[i];
				if (i == 0)
					str = g_string_new((gchar*) info_node->content);
				else
					g_string_append_printf(str, " -- %s", info_node->content);
			}
			purple_notify_user_info_add_pair(user_info, "Research", str->str);
			g_string_free(str, TRUE);
		}
		xmlXPathFreeObject(xpathObj);

		purple_notify_userinfo(gaa->pc, request->gabuddy->buddy->name, user_info, NULL, NULL);
		purple_notify_user_info_destroy(user_info);
	}

	/* Cleanup */
	xmlXPathFreeContext(xpathCtx);
	xmlFreeDoc(doc);

	/* Chained Callback */
	if (request->callback)
		request->callback(gaa, request->callback_data);
}

void ga_request_info(GayAttitudeAccount* gaa, const char *who, gboolean advertise, GayAttitudeRequestInfoCallbackFunc callback, gpointer callback_data)
{
	gchar *url_path;
	GayAttitudeBuddyInfoRequest *request = g_new0(GayAttitudeBuddyInfoRequest, TRUE);
	GayAttitudeBuddy *gabuddy;

	gabuddy = ga_find_gabuddy(gaa, who);
	if (!gabuddy)
		return;

	url_path = g_strdup_printf("/%s", who);
	request->gabuddy = gabuddy;
	request->advertise = advertise;
	request->callback = callback;
	request->callback_data = callback_data;
	http_post_or_get(gaa->http_handler, HTTP_METHOD_GET, GA_HOSTNAME_PERSO, url_path,
			NULL, ga_parse_contact_info, (gpointer) request, FALSE);
	g_free(url_path);
}
