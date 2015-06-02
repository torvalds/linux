/**
 * Copyright (C) ARM Limited 2013-2015. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "EventsXML.h"

#include "CapturedXML.h"
#include "Logging.h"
#include "OlyUtility.h"
#include "SessionData.h"

class XMLList {
public:
	XMLList(XMLList *const prev, mxml_node_t *const node) : mPrev(prev), mNode(node) {}

	XMLList *getPrev() { return mPrev; }
	mxml_node_t *getNode() const { return mNode; }
	void setNode(mxml_node_t *const node) { mNode = node; }

	static void free(XMLList *list) {
		while (list != NULL) {
			XMLList *prev = list->getPrev();
			delete list;
			list = prev;
		}
	}

private:
	XMLList *const mPrev;
	mxml_node_t *mNode;

	// Intentionally unimplemented
	XMLList(const XMLList &);
	XMLList &operator=(const XMLList &);
};

mxml_node_t *EventsXML::getTree() {
#include "events_xml.h" // defines and initializes char events_xml[] and int events_xml_len
	char path[PATH_MAX];
	mxml_node_t *xml = NULL;
	FILE *fl;

	// Avoid unused variable warning
	(void)events_xml_len;

	// Load the provided or default events xml
	if (gSessionData->mEventsXMLPath) {
		strncpy(path, gSessionData->mEventsXMLPath, PATH_MAX);
		fl = fopen(path, "r");
		if (fl) {
			xml = mxmlLoadFile(NULL, fl, MXML_NO_CALLBACK);
			fclose(fl);
		}
	}
	if (xml == NULL) {
		logg->logMessage("Unable to locate events.xml, using default");
		xml = mxmlLoadString(NULL, (const char *)events_xml, MXML_NO_CALLBACK);
	}

	// Append additional events XML
	if (gSessionData->mEventsXMLAppend) {
		fl = fopen(gSessionData->mEventsXMLAppend, "r");
		if (fl == NULL) {
			logg->logError("Unable to open additional events XML %s", gSessionData->mEventsXMLAppend);
			handleException();
		}
		mxml_node_t *append = mxmlLoadFile(NULL, fl, MXML_NO_CALLBACK);
		fclose(fl);

		mxml_node_t *events = mxmlFindElement(xml, xml, "events", NULL, NULL, MXML_DESCEND);
		if (!events) {
			logg->logError("Unable to find <events> node in the events.xml, please ensure the first two lines of events XML starts with:\n"
				       "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
				       "<events>");
			handleException();
		}

		XMLList *categoryList = NULL;
		XMLList *eventList = NULL;
		{
			// Make list of all categories in xml
			mxml_node_t *node = xml;
			while (true) {
				node = mxmlFindElement(node, xml, "category", NULL, NULL, MXML_DESCEND);
				if (node == NULL) {
					break;
				}
				categoryList = new XMLList(categoryList, node);
			}

			// Make list of all events in xml
			node = xml;
			while (true) {
				node = mxmlFindElement(node, xml, "event", NULL, NULL, MXML_DESCEND);
				if (node == NULL) {
					break;
				}
				eventList = new XMLList(eventList, node);
			}
		}

		// Handle events
		for (mxml_node_t *node = mxmlFindElement(append, append, "event", NULL, NULL, MXML_DESCEND),
		       *next = mxmlFindElement(node, append, "event", NULL, NULL, MXML_DESCEND);
		     node != NULL;
		     node = next, next = mxmlFindElement(node, append, "event", NULL, NULL, MXML_DESCEND)) {
			const char *const category = mxmlElementGetAttr(mxmlGetParent(node), "name");
			const char *const title = mxmlElementGetAttr(node, "title");
			const char *const name = mxmlElementGetAttr(node, "name");
			if (category == NULL || title == NULL || name == NULL) {
				logg->logError("Not all event XML nodes have the required title and name and parent name attributes");
				handleException();
			}

			// Replace any duplicate events
			for (XMLList *event = eventList; event != NULL; event = event->getPrev()) {
				const char *const category2 = mxmlElementGetAttr(mxmlGetParent(event->getNode()), "name");
				const char *const title2 = mxmlElementGetAttr(event->getNode(), "title");
				const char *const name2 = mxmlElementGetAttr(event->getNode(), "name");
				if (category2 == NULL || title2 == NULL || name2 == NULL) {
					logg->logError("Not all event XML nodes have the required title and name and parent name attributes");
					handleException();
				}

				if (strcmp(category, category2) == 0 && strcmp(title, title2) == 0 && strcmp(name, name2) == 0) {
					logg->logMessage("Replacing counter %s %s: %s", category, title, name);
					mxml_node_t *parent = mxmlGetParent(event->getNode());
					mxmlDelete(event->getNode());
					mxmlAdd(parent, MXML_ADD_AFTER, MXML_ADD_TO_PARENT, node);
					event->setNode(node);
					break;
				}
			}
		}

		// Handle categories
		for (mxml_node_t *node = strcmp(mxmlGetElement(append), "category") == 0 ? append : mxmlFindElement(append, append, "category", NULL, NULL, MXML_DESCEND),
		       *next = mxmlFindElement(node, append, "category", NULL, NULL, MXML_DESCEND);
		     node != NULL;
		     node = next, next = mxmlFindElement(node, append, "category", NULL, NULL, MXML_DESCEND)) {
			// After replacing duplicate events, a category may be empty
			if (mxmlGetFirstChild(node) == NULL) {
				continue;
			}

			const char *const name = mxmlElementGetAttr(node, "name");
			if (name == NULL) {
				logg->logError("Not all event XML categories have the required name attribute");
				handleException();
			}

			// Merge identically named categories
			bool merged = false;
			for (XMLList *category = categoryList; category != NULL; category = category->getPrev()) {
				const char *const name2 = mxmlElementGetAttr(category->getNode(), "name");
				if (name2 == NULL) {
					logg->logError("Not all event XML categories have the required name attribute");
					handleException();
				}

				if (strcmp(name, name2) == 0) {
					logg->logMessage("Merging category %s", name);
					while (true) {
						mxml_node_t *child = mxmlGetFirstChild(node);
						if (child == NULL) {
							break;
						}
						mxmlAdd(category->getNode(), MXML_ADD_AFTER, mxmlGetLastChild(category->getNode()), child);
					}
					merged = true;
					break;
				}
			}

			if (merged) {
				continue;
			}

			// Add new categories
			logg->logMessage("Appending category %s", name);
			mxmlAdd(events, MXML_ADD_AFTER, mxmlGetLastChild(events), node);
		}

		XMLList::free(eventList);
		XMLList::free(categoryList);

		mxmlDelete(append);
	}

	return xml;
}

char *EventsXML::getXML() {
	mxml_node_t *xml = getTree();

	// Add dynamic events from the drivers
	mxml_node_t *events = mxmlFindElement(xml, xml, "events", NULL, NULL, MXML_DESCEND);
	if (!events) {
		logg->logError("Unable to find <events> node in the events.xml, please ensure the first two lines of events XML are:\n"
			       "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
			       "<events>");
		handleException();
	}
	for (Driver *driver = Driver::getHead(); driver != NULL; driver = driver->getNext()) {
		driver->writeEvents(events);
	}

	char *string = mxmlSaveAllocString(xml, mxmlWhitespaceCB);
	mxmlDelete(xml);

	return string;
}

void EventsXML::write(const char *path) {
	char file[PATH_MAX];

	// Set full path
	snprintf(file, PATH_MAX, "%s/events.xml", path);

	char *buf = getXML();
	if (util->writeToDisk(file, buf) < 0) {
		logg->logError("Error writing %s\nPlease verify the path.", file);
		handleException();
	}

	free(buf);
}
