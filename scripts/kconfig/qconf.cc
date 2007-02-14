/*
 * Copyright (C) 2002 Roman Zippel <zippel@linux-m68k.org>
 * Released under the terms of the GNU GPL v2.0.
 */

#include <qapplication.h>
#include <qmainwindow.h>
#include <qtoolbar.h>
#include <qlayout.h>
#include <qvbox.h>
#include <qsplitter.h>
#include <qlistview.h>
#include <qtextbrowser.h>
#include <qlineedit.h>
#include <qlabel.h>
#include <qpushbutton.h>
#include <qmenubar.h>
#include <qmessagebox.h>
#include <qaction.h>
#include <qheader.h>
#include <qfiledialog.h>
#include <qdragobject.h>
#include <qregexp.h>

#include <stdlib.h>

#include "lkc.h"
#include "qconf.h"

#include "qconf.moc"
#include "images.c"

#ifdef _
# undef _
# define _ qgettext
#endif

static QApplication *configApp;
static ConfigSettings *configSettings;

QAction *ConfigMainWindow::saveAction;

static inline QString qgettext(const char* str)
{
	return QString::fromLocal8Bit(gettext(str));
}

static inline QString qgettext(const QString& str)
{
	return QString::fromLocal8Bit(gettext(str.latin1()));
}

/**
 * Reads a list of integer values from the application settings.
 */
QValueList<int> ConfigSettings::readSizes(const QString& key, bool *ok)
{
	QValueList<int> result;
	QStringList entryList = readListEntry(key, ok);
	if (ok) {
		QStringList::Iterator it;
		for (it = entryList.begin(); it != entryList.end(); ++it)
			result.push_back((*it).toInt());
	}

	return result;
}

/**
 * Writes a list of integer values to the application settings.
 */
bool ConfigSettings::writeSizes(const QString& key, const QValueList<int>& value)
{
	QStringList stringList;
	QValueList<int>::ConstIterator it;

	for (it = value.begin(); it != value.end(); ++it)
		stringList.push_back(QString::number(*it));
	return writeEntry(key, stringList);
}


#if QT_VERSION >= 300
/*
 * set the new data
 * TODO check the value
 */
void ConfigItem::okRename(int col)
{
	Parent::okRename(col);
	sym_set_string_value(menu->sym, text(dataColIdx).latin1());
	listView()->updateList(this);
}
#endif

/*
 * update the displayed of a menu entry
 */
void ConfigItem::updateMenu(void)
{
	ConfigList* list;
	struct symbol* sym;
	struct property *prop;
	QString prompt;
	int type;
	tristate expr;

	list = listView();
	if (goParent) {
		setPixmap(promptColIdx, list->menuBackPix);
		prompt = "..";
		goto set_prompt;
	}

	sym = menu->sym;
	prop = menu->prompt;
	prompt = QString::fromLocal8Bit(menu_get_prompt(menu));

	if (prop) switch (prop->type) {
	case P_MENU:
		if (list->mode == singleMode || list->mode == symbolMode) {
			/* a menuconfig entry is displayed differently
			 * depending whether it's at the view root or a child.
			 */
			if (sym && list->rootEntry == menu)
				break;
			setPixmap(promptColIdx, list->menuPix);
		} else {
			if (sym)
				break;
			setPixmap(promptColIdx, 0);
		}
		goto set_prompt;
	case P_COMMENT:
		setPixmap(promptColIdx, 0);
		goto set_prompt;
	default:
		;
	}
	if (!sym)
		goto set_prompt;

	setText(nameColIdx, QString::fromLocal8Bit(sym->name));

	type = sym_get_type(sym);
	switch (type) {
	case S_BOOLEAN:
	case S_TRISTATE:
		char ch;

		if (!sym_is_changable(sym) && !list->showAll) {
			setPixmap(promptColIdx, 0);
			setText(noColIdx, QString::null);
			setText(modColIdx, QString::null);
			setText(yesColIdx, QString::null);
			break;
		}
		expr = sym_get_tristate_value(sym);
		switch (expr) {
		case yes:
			if (sym_is_choice_value(sym) && type == S_BOOLEAN)
				setPixmap(promptColIdx, list->choiceYesPix);
			else
				setPixmap(promptColIdx, list->symbolYesPix);
			setText(yesColIdx, "Y");
			ch = 'Y';
			break;
		case mod:
			setPixmap(promptColIdx, list->symbolModPix);
			setText(modColIdx, "M");
			ch = 'M';
			break;
		default:
			if (sym_is_choice_value(sym) && type == S_BOOLEAN)
				setPixmap(promptColIdx, list->choiceNoPix);
			else
				setPixmap(promptColIdx, list->symbolNoPix);
			setText(noColIdx, "N");
			ch = 'N';
			break;
		}
		if (expr != no)
			setText(noColIdx, sym_tristate_within_range(sym, no) ? "_" : 0);
		if (expr != mod)
			setText(modColIdx, sym_tristate_within_range(sym, mod) ? "_" : 0);
		if (expr != yes)
			setText(yesColIdx, sym_tristate_within_range(sym, yes) ? "_" : 0);

		setText(dataColIdx, QChar(ch));
		break;
	case S_INT:
	case S_HEX:
	case S_STRING:
		const char* data;

		data = sym_get_string_value(sym);

#if QT_VERSION >= 300
		int i = list->mapIdx(dataColIdx);
		if (i >= 0)
			setRenameEnabled(i, TRUE);
#endif
		setText(dataColIdx, data);
		if (type == S_STRING)
			prompt = QString("%1: %2").arg(prompt).arg(data);
		else
			prompt = QString("(%2) %1").arg(prompt).arg(data);
		break;
	}
	if (!sym_has_value(sym) && visible)
		prompt += " (NEW)";
set_prompt:
	setText(promptColIdx, prompt);
}

void ConfigItem::testUpdateMenu(bool v)
{
	ConfigItem* i;

	visible = v;
	if (!menu)
		return;

	sym_calc_value(menu->sym);
	if (menu->flags & MENU_CHANGED) {
		/* the menu entry changed, so update all list items */
		menu->flags &= ~MENU_CHANGED;
		for (i = (ConfigItem*)menu->data; i; i = i->nextItem)
			i->updateMenu();
	} else if (listView()->updateAll)
		updateMenu();
}

void ConfigItem::paintCell(QPainter* p, const QColorGroup& cg, int column, int width, int align)
{
	ConfigList* list = listView();

	if (visible) {
		if (isSelected() && !list->hasFocus() && list->mode == menuMode)
			Parent::paintCell(p, list->inactivedColorGroup, column, width, align);
		else
			Parent::paintCell(p, cg, column, width, align);
	} else
		Parent::paintCell(p, list->disabledColorGroup, column, width, align);
}

/*
 * construct a menu entry
 */
void ConfigItem::init(void)
{
	if (menu) {
		ConfigList* list = listView();
		nextItem = (ConfigItem*)menu->data;
		menu->data = this;

		if (list->mode != fullMode)
			setOpen(TRUE);
		sym_calc_value(menu->sym);
	}
	updateMenu();
}

/*
 * destruct a menu entry
 */
ConfigItem::~ConfigItem(void)
{
	if (menu) {
		ConfigItem** ip = (ConfigItem**)&menu->data;
		for (; *ip; ip = &(*ip)->nextItem) {
			if (*ip == this) {
				*ip = nextItem;
				break;
			}
		}
	}
}

ConfigLineEdit::ConfigLineEdit(ConfigView* parent)
	: Parent(parent)
{
	connect(this, SIGNAL(lostFocus()), SLOT(hide()));
}

void ConfigLineEdit::show(ConfigItem* i)
{
	item = i;
	if (sym_get_string_value(item->menu->sym))
		setText(QString::fromLocal8Bit(sym_get_string_value(item->menu->sym)));
	else
		setText(QString::null);
	Parent::show();
	setFocus();
}

void ConfigLineEdit::keyPressEvent(QKeyEvent* e)
{
	switch (e->key()) {
	case Key_Escape:
		break;
	case Key_Return:
	case Key_Enter:
		sym_set_string_value(item->menu->sym, text().latin1());
		parent()->updateList(item);
		break;
	default:
		Parent::keyPressEvent(e);
		return;
	}
	e->accept();
	parent()->list->setFocus();
	hide();
}

ConfigList::ConfigList(ConfigView* p, const char *name)
	: Parent(p, name),
	  updateAll(false),
	  symbolYesPix(xpm_symbol_yes), symbolModPix(xpm_symbol_mod), symbolNoPix(xpm_symbol_no),
	  choiceYesPix(xpm_choice_yes), choiceNoPix(xpm_choice_no),
	  menuPix(xpm_menu), menuInvPix(xpm_menu_inv), menuBackPix(xpm_menuback), voidPix(xpm_void),
	  showAll(false), showName(false), showRange(false), showData(false),
	  rootEntry(0), headerPopup(0)
{
	int i;

	setSorting(-1);
	setRootIsDecorated(TRUE);
	disabledColorGroup = palette().active();
	disabledColorGroup.setColor(QColorGroup::Text, palette().disabled().text());
	inactivedColorGroup = palette().active();
	inactivedColorGroup.setColor(QColorGroup::Highlight, palette().disabled().highlight());

	connect(this, SIGNAL(selectionChanged(void)),
		SLOT(updateSelection(void)));

	if (name) {
		configSettings->beginGroup(name);
		showAll = configSettings->readBoolEntry("/showAll", false);
		showName = configSettings->readBoolEntry("/showName", false);
		showRange = configSettings->readBoolEntry("/showRange", false);
		showData = configSettings->readBoolEntry("/showData", false);
		configSettings->endGroup();
		connect(configApp, SIGNAL(aboutToQuit()), SLOT(saveSettings()));
	}

	for (i = 0; i < colNr; i++)
		colMap[i] = colRevMap[i] = -1;
	addColumn(promptColIdx, "Option");

	reinit();
}

void ConfigList::reinit(void)
{
	removeColumn(dataColIdx);
	removeColumn(yesColIdx);
	removeColumn(modColIdx);
	removeColumn(noColIdx);
	removeColumn(nameColIdx);

	if (showName)
		addColumn(nameColIdx, "Name");
	if (showRange) {
		addColumn(noColIdx, "N");
		addColumn(modColIdx, "M");
		addColumn(yesColIdx, "Y");
	}
	if (showData)
		addColumn(dataColIdx, "Value");

	updateListAll();
}

void ConfigList::saveSettings(void)
{
	if (name()) {
		configSettings->beginGroup(name());
		configSettings->writeEntry("/showName", showName);
		configSettings->writeEntry("/showRange", showRange);
		configSettings->writeEntry("/showData", showData);
		configSettings->writeEntry("/showAll", showAll);
		configSettings->endGroup();
	}
}

ConfigItem* ConfigList::findConfigItem(struct menu *menu)
{
	ConfigItem* item = (ConfigItem*)menu->data;

	for (; item; item = item->nextItem) {
		if (this == item->listView())
			break;
	}

	return item;
}

void ConfigList::updateSelection(void)
{
	struct menu *menu;
	enum prop_type type;

	ConfigItem* item = (ConfigItem*)selectedItem();
	if (!item)
		return;

	menu = item->menu;
	emit menuChanged(menu);
	if (!menu)
		return;
	type = menu->prompt ? menu->prompt->type : P_UNKNOWN;
	if (mode == menuMode && type == P_MENU)
		emit menuSelected(menu);
}

void ConfigList::updateList(ConfigItem* item)
{
	ConfigItem* last = 0;

	if (!rootEntry) {
		if (mode != listMode)
			goto update;
		QListViewItemIterator it(this);
		ConfigItem* item;

		for (; it.current(); ++it) {
			item = (ConfigItem*)it.current();
			if (!item->menu)
				continue;
			item->testUpdateMenu(menu_is_visible(item->menu));
		}
		return;
	}

	if (rootEntry != &rootmenu && (mode == singleMode ||
	    (mode == symbolMode && rootEntry->parent != &rootmenu))) {
		item = firstChild();
		if (!item)
			item = new ConfigItem(this, 0, true);
		last = item;
	}
	if ((mode == singleMode || (mode == symbolMode && !(rootEntry->flags & MENU_ROOT))) &&
	    rootEntry->sym && rootEntry->prompt) {
		item = last ? last->nextSibling() : firstChild();
		if (!item)
			item = new ConfigItem(this, last, rootEntry, true);
		else
			item->testUpdateMenu(true);

		updateMenuList(item, rootEntry);
		triggerUpdate();
		return;
	}
update:
	updateMenuList(this, rootEntry);
	triggerUpdate();
}

void ConfigList::setValue(ConfigItem* item, tristate val)
{
	struct symbol* sym;
	int type;
	tristate oldval;

	sym = item->menu ? item->menu->sym : 0;
	if (!sym)
		return;

	type = sym_get_type(sym);
	switch (type) {
	case S_BOOLEAN:
	case S_TRISTATE:
		oldval = sym_get_tristate_value(sym);

		if (!sym_set_tristate_value(sym, val))
			return;
		if (oldval == no && item->menu->list)
			item->setOpen(TRUE);
		parent()->updateList(item);
		break;
	}
}

void ConfigList::changeValue(ConfigItem* item)
{
	struct symbol* sym;
	struct menu* menu;
	int type, oldexpr, newexpr;

	menu = item->menu;
	if (!menu)
		return;
	sym = menu->sym;
	if (!sym) {
		if (item->menu->list)
			item->setOpen(!item->isOpen());
		return;
	}

	type = sym_get_type(sym);
	switch (type) {
	case S_BOOLEAN:
	case S_TRISTATE:
		oldexpr = sym_get_tristate_value(sym);
		newexpr = sym_toggle_tristate_value(sym);
		if (item->menu->list) {
			if (oldexpr == newexpr)
				item->setOpen(!item->isOpen());
			else if (oldexpr == no)
				item->setOpen(TRUE);
		}
		if (oldexpr != newexpr)
			parent()->updateList(item);
		break;
	case S_INT:
	case S_HEX:
	case S_STRING:
#if QT_VERSION >= 300
		if (colMap[dataColIdx] >= 0)
			item->startRename(colMap[dataColIdx]);
		else
#endif
			parent()->lineEdit->show(item);
		break;
	}
}

void ConfigList::setRootMenu(struct menu *menu)
{
	enum prop_type type;

	if (rootEntry == menu)
		return;
	type = menu && menu->prompt ? menu->prompt->type : P_UNKNOWN;
	if (type != P_MENU)
		return;
	updateMenuList(this, 0);
	rootEntry = menu;
	updateListAll();
	setSelected(currentItem(), hasFocus());
	ensureItemVisible(currentItem());
}

void ConfigList::setParentMenu(void)
{
	ConfigItem* item;
	struct menu *oldroot;

	oldroot = rootEntry;
	if (rootEntry == &rootmenu)
		return;
	setRootMenu(menu_get_parent_menu(rootEntry->parent));

	QListViewItemIterator it(this);
	for (; (item = (ConfigItem*)it.current()); it++) {
		if (item->menu == oldroot) {
			setCurrentItem(item);
			ensureItemVisible(item);
			break;
		}
	}
}

/*
 * update all the children of a menu entry
 *   removes/adds the entries from the parent widget as necessary
 *
 * parent: either the menu list widget or a menu entry widget
 * menu: entry to be updated
 */
template <class P>
void ConfigList::updateMenuList(P* parent, struct menu* menu)
{
	struct menu* child;
	ConfigItem* item;
	ConfigItem* last;
	bool visible;
	enum prop_type type;

	if (!menu) {
		while ((item = parent->firstChild()))
			delete item;
		return;
	}

	last = parent->firstChild();
	if (last && !last->goParent)
		last = 0;
	for (child = menu->list; child; child = child->next) {
		item = last ? last->nextSibling() : parent->firstChild();
		type = child->prompt ? child->prompt->type : P_UNKNOWN;

		switch (mode) {
		case menuMode:
			if (!(child->flags & MENU_ROOT))
				goto hide;
			break;
		case symbolMode:
			if (child->flags & MENU_ROOT)
				goto hide;
			break;
		default:
			break;
		}

		visible = menu_is_visible(child);
		if (showAll || visible) {
			if (!child->sym && !child->list && !child->prompt)
				continue;
			if (!item || item->menu != child)
				item = new ConfigItem(parent, last, child, visible);
			else
				item->testUpdateMenu(visible);

			if (mode == fullMode || mode == menuMode || type != P_MENU)
				updateMenuList(item, child);
			else
				updateMenuList(item, 0);
			last = item;
			continue;
		}
	hide:
		if (item && item->menu == child) {
			last = parent->firstChild();
			if (last == item)
				last = 0;
			else while (last->nextSibling() != item)
				last = last->nextSibling();
			delete item;
		}
	}
}

void ConfigList::keyPressEvent(QKeyEvent* ev)
{
	QListViewItem* i = currentItem();
	ConfigItem* item;
	struct menu *menu;
	enum prop_type type;

	if (ev->key() == Key_Escape && mode != fullMode && mode != listMode) {
		emit parentSelected();
		ev->accept();
		return;
	}

	if (!i) {
		Parent::keyPressEvent(ev);
		return;
	}
	item = (ConfigItem*)i;

	switch (ev->key()) {
	case Key_Return:
	case Key_Enter:
		if (item->goParent) {
			emit parentSelected();
			break;
		}
		menu = item->menu;
		if (!menu)
			break;
		type = menu->prompt ? menu->prompt->type : P_UNKNOWN;
		if (type == P_MENU && rootEntry != menu &&
		    mode != fullMode && mode != menuMode) {
			emit menuSelected(menu);
			break;
		}
	case Key_Space:
		changeValue(item);
		break;
	case Key_N:
		setValue(item, no);
		break;
	case Key_M:
		setValue(item, mod);
		break;
	case Key_Y:
		setValue(item, yes);
		break;
	default:
		Parent::keyPressEvent(ev);
		return;
	}
	ev->accept();
}

void ConfigList::contentsMousePressEvent(QMouseEvent* e)
{
	//QPoint p(contentsToViewport(e->pos()));
	//printf("contentsMousePressEvent: %d,%d\n", p.x(), p.y());
	Parent::contentsMousePressEvent(e);
}

void ConfigList::contentsMouseReleaseEvent(QMouseEvent* e)
{
	QPoint p(contentsToViewport(e->pos()));
	ConfigItem* item = (ConfigItem*)itemAt(p);
	struct menu *menu;
	enum prop_type ptype;
	const QPixmap* pm;
	int idx, x;

	if (!item)
		goto skip;

	menu = item->menu;
	x = header()->offset() + p.x();
	idx = colRevMap[header()->sectionAt(x)];
	switch (idx) {
	case promptColIdx:
		pm = item->pixmap(promptColIdx);
		if (pm) {
			int off = header()->sectionPos(0) + itemMargin() +
				treeStepSize() * (item->depth() + (rootIsDecorated() ? 1 : 0));
			if (x >= off && x < off + pm->width()) {
				if (item->goParent) {
					emit parentSelected();
					break;
				} else if (!menu)
					break;
				ptype = menu->prompt ? menu->prompt->type : P_UNKNOWN;
				if (ptype == P_MENU && rootEntry != menu &&
				    mode != fullMode && mode != menuMode)
					emit menuSelected(menu);
				else
					changeValue(item);
			}
		}
		break;
	case noColIdx:
		setValue(item, no);
		break;
	case modColIdx:
		setValue(item, mod);
		break;
	case yesColIdx:
		setValue(item, yes);
		break;
	case dataColIdx:
		changeValue(item);
		break;
	}

skip:
	//printf("contentsMouseReleaseEvent: %d,%d\n", p.x(), p.y());
	Parent::contentsMouseReleaseEvent(e);
}

void ConfigList::contentsMouseMoveEvent(QMouseEvent* e)
{
	//QPoint p(contentsToViewport(e->pos()));
	//printf("contentsMouseMoveEvent: %d,%d\n", p.x(), p.y());
	Parent::contentsMouseMoveEvent(e);
}

void ConfigList::contentsMouseDoubleClickEvent(QMouseEvent* e)
{
	QPoint p(contentsToViewport(e->pos()));
	ConfigItem* item = (ConfigItem*)itemAt(p);
	struct menu *menu;
	enum prop_type ptype;

	if (!item)
		goto skip;
	if (item->goParent) {
		emit parentSelected();
		goto skip;
	}
	menu = item->menu;
	if (!menu)
		goto skip;
	ptype = menu->prompt ? menu->prompt->type : P_UNKNOWN;
	if (ptype == P_MENU && (mode == singleMode || mode == symbolMode))
		emit menuSelected(menu);
	else if (menu->sym)
		changeValue(item);

skip:
	//printf("contentsMouseDoubleClickEvent: %d,%d\n", p.x(), p.y());
	Parent::contentsMouseDoubleClickEvent(e);
}

void ConfigList::focusInEvent(QFocusEvent *e)
{
	struct menu *menu = NULL;

	Parent::focusInEvent(e);

	ConfigItem* item = (ConfigItem *)currentItem();
	if (item) {
		setSelected(item, TRUE);
		menu = item->menu;
	}
	emit gotFocus(menu);
}

void ConfigList::contextMenuEvent(QContextMenuEvent *e)
{
	if (e->y() <= header()->geometry().bottom()) {
		if (!headerPopup) {
			QAction *action;

			headerPopup = new QPopupMenu(this);
			action = new QAction(NULL, "Show Name", 0, this);
			  action->setToggleAction(TRUE);
			  connect(action, SIGNAL(toggled(bool)),
				  parent(), SLOT(setShowName(bool)));
			  connect(parent(), SIGNAL(showNameChanged(bool)),
				  action, SLOT(setOn(bool)));
			  action->setOn(showName);
			  action->addTo(headerPopup);
			action = new QAction(NULL, "Show Range", 0, this);
			  action->setToggleAction(TRUE);
			  connect(action, SIGNAL(toggled(bool)),
				  parent(), SLOT(setShowRange(bool)));
			  connect(parent(), SIGNAL(showRangeChanged(bool)),
				  action, SLOT(setOn(bool)));
			  action->setOn(showRange);
			  action->addTo(headerPopup);
			action = new QAction(NULL, "Show Data", 0, this);
			  action->setToggleAction(TRUE);
			  connect(action, SIGNAL(toggled(bool)),
				  parent(), SLOT(setShowData(bool)));
			  connect(parent(), SIGNAL(showDataChanged(bool)),
				  action, SLOT(setOn(bool)));
			  action->setOn(showData);
			  action->addTo(headerPopup);
		}
		headerPopup->exec(e->globalPos());
		e->accept();
	} else
		e->ignore();
}

ConfigView* ConfigView::viewList;

ConfigView::ConfigView(QWidget* parent, const char *name)
	: Parent(parent, name)
{
	list = new ConfigList(this, name);
	lineEdit = new ConfigLineEdit(this);
	lineEdit->hide();

	this->nextView = viewList;
	viewList = this;
}

ConfigView::~ConfigView(void)
{
	ConfigView** vp;

	for (vp = &viewList; *vp; vp = &(*vp)->nextView) {
		if (*vp == this) {
			*vp = nextView;
			break;
		}
	}
}

void ConfigView::setShowAll(bool b)
{
	if (list->showAll != b) {
		list->showAll = b;
		list->updateListAll();
		emit showAllChanged(b);
	}
}

void ConfigView::setShowName(bool b)
{
	if (list->showName != b) {
		list->showName = b;
		list->reinit();
		emit showNameChanged(b);
	}
}

void ConfigView::setShowRange(bool b)
{
	if (list->showRange != b) {
		list->showRange = b;
		list->reinit();
		emit showRangeChanged(b);
	}
}

void ConfigView::setShowData(bool b)
{
	if (list->showData != b) {
		list->showData = b;
		list->reinit();
		emit showDataChanged(b);
	}
}

void ConfigList::setAllOpen(bool open)
{
	QListViewItemIterator it(this);

	for (; it.current(); it++)
		it.current()->setOpen(open);
}

void ConfigView::updateList(ConfigItem* item)
{
	ConfigView* v;

	for (v = viewList; v; v = v->nextView)
		v->list->updateList(item);
}

void ConfigView::updateListAll(void)
{
	ConfigView* v;

	for (v = viewList; v; v = v->nextView)
		v->list->updateListAll();
}

ConfigInfoView::ConfigInfoView(QWidget* parent, const char *name)
	: Parent(parent, name), menu(0), sym(0)
{
	if (name) {
		configSettings->beginGroup(name);
		_showDebug = configSettings->readBoolEntry("/showDebug", false);
		configSettings->endGroup();
		connect(configApp, SIGNAL(aboutToQuit()), SLOT(saveSettings()));
	}
}

void ConfigInfoView::saveSettings(void)
{
	if (name()) {
		configSettings->beginGroup(name());
		configSettings->writeEntry("/showDebug", showDebug());
		configSettings->endGroup();
	}
}

void ConfigInfoView::setShowDebug(bool b)
{
	if (_showDebug != b) {
		_showDebug = b;
		if (menu)
			menuInfo();
		else if (sym)
			symbolInfo();
		emit showDebugChanged(b);
	}
}

void ConfigInfoView::setInfo(struct menu *m)
{
	if (menu == m)
		return;
	menu = m;
	sym = NULL;
	if (!menu)
		clear();
	else
		menuInfo();
}

void ConfigInfoView::setSource(const QString& name)
{
	const char *p = name.latin1();

	menu = NULL;
	sym = NULL;

	switch (p[0]) {
	case 'm':
		struct menu *m;

		if (sscanf(p, "m%p", &m) == 1 && menu != m) {
			menu = m;
			menuInfo();
			emit menuSelected(menu);
		}
		break;
	case 's':
		struct symbol *s;

		if (sscanf(p, "s%p", &s) == 1 && sym != s) {
			sym = s;
			symbolInfo();
		}
		break;
	}
}

void ConfigInfoView::symbolInfo(void)
{
	QString str;

	str += "<big>Symbol: <b>";
	str += print_filter(sym->name);
	str += "</b></big><br><br>value: ";
	str += print_filter(sym_get_string_value(sym));
	str += "<br>visibility: ";
	str += sym->visible == yes ? "y" : sym->visible == mod ? "m" : "n";
	str += "<br>";
	str += debug_info(sym);

	setText(str);
}

void ConfigInfoView::menuInfo(void)
{
	struct symbol* sym;
	QString head, debug, help;

	sym = menu->sym;
	if (sym) {
		if (menu->prompt) {
			head += "<big><b>";
			head += print_filter(_(menu->prompt->text));
			head += "</b></big>";
			if (sym->name) {
				head += " (";
				if (showDebug())
					head += QString().sprintf("<a href=\"s%p\">", sym);
				head += print_filter(sym->name);
				if (showDebug())
					head += "</a>";
				head += ")";
			}
		} else if (sym->name) {
			head += "<big><b>";
			if (showDebug())
				head += QString().sprintf("<a href=\"s%p\">", sym);
			head += print_filter(sym->name);
			if (showDebug())
				head += "</a>";
			head += "</b></big>";
		}
		head += "<br><br>";

		if (showDebug())
			debug = debug_info(sym);

		help = print_filter(_(sym->help));
	} else if (menu->prompt) {
		head += "<big><b>";
		head += print_filter(_(menu->prompt->text));
		head += "</b></big><br><br>";
		if (showDebug()) {
			if (menu->prompt->visible.expr) {
				debug += "&nbsp;&nbsp;dep: ";
				expr_print(menu->prompt->visible.expr, expr_print_help, &debug, E_NONE);
				debug += "<br><br>";
			}
		}
	}
	if (showDebug())
		debug += QString().sprintf("defined at %s:%d<br><br>", menu->file->name, menu->lineno);

	setText(head + debug + help);
}

QString ConfigInfoView::debug_info(struct symbol *sym)
{
	QString debug;

	debug += "type: ";
	debug += print_filter(sym_type_name(sym->type));
	if (sym_is_choice(sym))
		debug += " (choice)";
	debug += "<br>";
	if (sym->rev_dep.expr) {
		debug += "reverse dep: ";
		expr_print(sym->rev_dep.expr, expr_print_help, &debug, E_NONE);
		debug += "<br>";
	}
	for (struct property *prop = sym->prop; prop; prop = prop->next) {
		switch (prop->type) {
		case P_PROMPT:
		case P_MENU:
			debug += QString().sprintf("prompt: <a href=\"m%p\">", prop->menu);
			debug += print_filter(_(prop->text));
			debug += "</a><br>";
			break;
		case P_DEFAULT:
			debug += "default: ";
			expr_print(prop->expr, expr_print_help, &debug, E_NONE);
			debug += "<br>";
			break;
		case P_CHOICE:
			if (sym_is_choice(sym)) {
				debug += "choice: ";
				expr_print(prop->expr, expr_print_help, &debug, E_NONE);
				debug += "<br>";
			}
			break;
		case P_SELECT:
			debug += "select: ";
			expr_print(prop->expr, expr_print_help, &debug, E_NONE);
			debug += "<br>";
			break;
		case P_RANGE:
			debug += "range: ";
			expr_print(prop->expr, expr_print_help, &debug, E_NONE);
			debug += "<br>";
			break;
		default:
			debug += "unknown property: ";
			debug += prop_get_type_name(prop->type);
			debug += "<br>";
		}
		if (prop->visible.expr) {
			debug += "&nbsp;&nbsp;&nbsp;&nbsp;dep: ";
			expr_print(prop->visible.expr, expr_print_help, &debug, E_NONE);
			debug += "<br>";
		}
	}
	debug += "<br>";

	return debug;
}

QString ConfigInfoView::print_filter(const QString &str)
{
	QRegExp re("[<>&\"\\n]");
	QString res = str;
	for (int i = 0; (i = res.find(re, i)) >= 0;) {
		switch (res[i].latin1()) {
		case '<':
			res.replace(i, 1, "&lt;");
			i += 4;
			break;
		case '>':
			res.replace(i, 1, "&gt;");
			i += 4;
			break;
		case '&':
			res.replace(i, 1, "&amp;");
			i += 5;
			break;
		case '"':
			res.replace(i, 1, "&quot;");
			i += 6;
			break;
		case '\n':
			res.replace(i, 1, "<br>");
			i += 4;
			break;
		}
	}
	return res;
}

void ConfigInfoView::expr_print_help(void *data, struct symbol *sym, const char *str)
{
	QString* text = reinterpret_cast<QString*>(data);
	QString str2 = print_filter(str);

	if (sym && sym->name && !(sym->flags & SYMBOL_CONST)) {
		*text += QString().sprintf("<a href=\"s%p\">", sym);
		*text += str2;
		*text += "</a>";
	} else
		*text += str2;
}

QPopupMenu* ConfigInfoView::createPopupMenu(const QPoint& pos)
{
	QPopupMenu* popup = Parent::createPopupMenu(pos);
	QAction* action = new QAction(NULL,"Show Debug Info", 0, popup);
	  action->setToggleAction(TRUE);
	  connect(action, SIGNAL(toggled(bool)), SLOT(setShowDebug(bool)));
	  connect(this, SIGNAL(showDebugChanged(bool)), action, SLOT(setOn(bool)));
	  action->setOn(showDebug());
	popup->insertSeparator();
	action->addTo(popup);
	return popup;
}

void ConfigInfoView::contentsContextMenuEvent(QContextMenuEvent *e)
{
	Parent::contentsContextMenuEvent(e);
}

ConfigSearchWindow::ConfigSearchWindow(QWidget* parent, const char *name)
	: Parent(parent, name), result(NULL)
{
	setCaption("Search Config");

	QVBoxLayout* layout1 = new QVBoxLayout(this, 11, 6);
	QHBoxLayout* layout2 = new QHBoxLayout(0, 0, 6);
	layout2->addWidget(new QLabel("Find:", this));
	editField = new QLineEdit(this);
	connect(editField, SIGNAL(returnPressed()), SLOT(search()));
	layout2->addWidget(editField);
	searchButton = new QPushButton("Search", this);
	searchButton->setAutoDefault(FALSE);
	connect(searchButton, SIGNAL(clicked()), SLOT(search()));
	layout2->addWidget(searchButton);
	layout1->addLayout(layout2);

	split = new QSplitter(this);
	split->setOrientation(QSplitter::Vertical);
	list = new ConfigView(split, name);
	list->list->mode = listMode;
	info = new ConfigInfoView(split, name);
	connect(list->list, SIGNAL(menuChanged(struct menu *)),
		info, SLOT(setInfo(struct menu *)));
	layout1->addWidget(split);

	if (name) {
		int x, y, width, height;
		bool ok;

		configSettings->beginGroup(name);
		width = configSettings->readNumEntry("/window width", parent->width() / 2);
		height = configSettings->readNumEntry("/window height", parent->height() / 2);
		resize(width, height);
		x = configSettings->readNumEntry("/window x", 0, &ok);
		if (ok)
			y = configSettings->readNumEntry("/window y", 0, &ok);
		if (ok)
			move(x, y);
		QValueList<int> sizes = configSettings->readSizes("/split", &ok);
		if (ok)
			split->setSizes(sizes);
		configSettings->endGroup();
		connect(configApp, SIGNAL(aboutToQuit()), SLOT(saveSettings()));
	}
}

void ConfigSearchWindow::saveSettings(void)
{
	if (name()) {
		configSettings->beginGroup(name());
		configSettings->writeEntry("/window x", pos().x());
		configSettings->writeEntry("/window y", pos().y());
		configSettings->writeEntry("/window width", size().width());
		configSettings->writeEntry("/window height", size().height());
		configSettings->writeSizes("/split", split->sizes());
		configSettings->endGroup();
	}
}

void ConfigSearchWindow::search(void)
{
	struct symbol **p;
	struct property *prop;
	ConfigItem *lastItem = NULL;

	free(result);
	list->list->clear();
	info->clear();

	result = sym_re_search(editField->text().latin1());
	if (!result)
		return;
	for (p = result; *p; p++) {
		for_all_prompts((*p), prop)
			lastItem = new ConfigItem(list->list, lastItem, prop->menu,
						  menu_is_visible(prop->menu));
	}
}

/*
 * Construct the complete config widget
 */
ConfigMainWindow::ConfigMainWindow(void)
	: searchWindow(0)
{
	QMenuBar* menu;
	bool ok;
	int x, y, width, height;

	QWidget *d = configApp->desktop();

	width = configSettings->readNumEntry("/window width", d->width() - 64);
	height = configSettings->readNumEntry("/window height", d->height() - 64);
	resize(width, height);
	x = configSettings->readNumEntry("/window x", 0, &ok);
	if (ok)
		y = configSettings->readNumEntry("/window y", 0, &ok);
	if (ok)
		move(x, y);

	split1 = new QSplitter(this);
	split1->setOrientation(QSplitter::Horizontal);
	setCentralWidget(split1);

	menuView = new ConfigView(split1, "menu");
	menuList = menuView->list;

	split2 = new QSplitter(split1);
	split2->setOrientation(QSplitter::Vertical);

	// create config tree
	configView = new ConfigView(split2, "config");
	configList = configView->list;

	helpText = new ConfigInfoView(split2, "help");
	helpText->setTextFormat(Qt::RichText);

	setTabOrder(configList, helpText);
	configList->setFocus();

	menu = menuBar();
	toolBar = new QToolBar("Tools", this);

	backAction = new QAction("Back", QPixmap(xpm_back), "Back", 0, this);
	  connect(backAction, SIGNAL(activated()), SLOT(goBack()));
	  backAction->setEnabled(FALSE);
	QAction *quitAction = new QAction("Quit", "&Quit", CTRL+Key_Q, this);
	  connect(quitAction, SIGNAL(activated()), SLOT(close()));
	QAction *loadAction = new QAction("Load", QPixmap(xpm_load), "&Load", CTRL+Key_L, this);
	  connect(loadAction, SIGNAL(activated()), SLOT(loadConfig()));
	saveAction = new QAction("Save", QPixmap(xpm_save), "&Save", CTRL+Key_S, this);
	  connect(saveAction, SIGNAL(activated()), SLOT(saveConfig()));
	conf_set_changed_callback(conf_changed);
	// Set saveAction's initial state
	conf_changed();
	QAction *saveAsAction = new QAction("Save As...", "Save &As...", 0, this);
	  connect(saveAsAction, SIGNAL(activated()), SLOT(saveConfigAs()));
	QAction *searchAction = new QAction("Find", "&Find", CTRL+Key_F, this);
	  connect(searchAction, SIGNAL(activated()), SLOT(searchConfig()));
	QAction *singleViewAction = new QAction("Single View", QPixmap(xpm_single_view), "Split View", 0, this);
	  connect(singleViewAction, SIGNAL(activated()), SLOT(showSingleView()));
	QAction *splitViewAction = new QAction("Split View", QPixmap(xpm_split_view), "Split View", 0, this);
	  connect(splitViewAction, SIGNAL(activated()), SLOT(showSplitView()));
	QAction *fullViewAction = new QAction("Full View", QPixmap(xpm_tree_view), "Full View", 0, this);
	  connect(fullViewAction, SIGNAL(activated()), SLOT(showFullView()));

	QAction *showNameAction = new QAction(NULL, "Show Name", 0, this);
	  showNameAction->setToggleAction(TRUE);
	  connect(showNameAction, SIGNAL(toggled(bool)), configView, SLOT(setShowName(bool)));
	  connect(configView, SIGNAL(showNameChanged(bool)), showNameAction, SLOT(setOn(bool)));
	  showNameAction->setOn(configView->showName());
	QAction *showRangeAction = new QAction(NULL, "Show Range", 0, this);
	  showRangeAction->setToggleAction(TRUE);
	  connect(showRangeAction, SIGNAL(toggled(bool)), configView, SLOT(setShowRange(bool)));
	  connect(configView, SIGNAL(showRangeChanged(bool)), showRangeAction, SLOT(setOn(bool)));
	  showRangeAction->setOn(configList->showRange);
	QAction *showDataAction = new QAction(NULL, "Show Data", 0, this);
	  showDataAction->setToggleAction(TRUE);
	  connect(showDataAction, SIGNAL(toggled(bool)), configView, SLOT(setShowData(bool)));
	  connect(configView, SIGNAL(showDataChanged(bool)), showDataAction, SLOT(setOn(bool)));
	  showDataAction->setOn(configList->showData);
	QAction *showAllAction = new QAction(NULL, "Show All Options", 0, this);
	  showAllAction->setToggleAction(TRUE);
	  connect(showAllAction, SIGNAL(toggled(bool)), configView, SLOT(setShowAll(bool)));
	  connect(showAllAction, SIGNAL(toggled(bool)), menuView, SLOT(setShowAll(bool)));
	  showAllAction->setOn(configList->showAll);
	QAction *showDebugAction = new QAction(NULL, "Show Debug Info", 0, this);
	  showDebugAction->setToggleAction(TRUE);
	  connect(showDebugAction, SIGNAL(toggled(bool)), helpText, SLOT(setShowDebug(bool)));
	  connect(helpText, SIGNAL(showDebugChanged(bool)), showDebugAction, SLOT(setOn(bool)));
	  showDebugAction->setOn(helpText->showDebug());

	QAction *showIntroAction = new QAction(NULL, "Introduction", 0, this);
	  connect(showIntroAction, SIGNAL(activated()), SLOT(showIntro()));
	QAction *showAboutAction = new QAction(NULL, "About", 0, this);
	  connect(showAboutAction, SIGNAL(activated()), SLOT(showAbout()));

	// init tool bar
	backAction->addTo(toolBar);
	toolBar->addSeparator();
	loadAction->addTo(toolBar);
	saveAction->addTo(toolBar);
	toolBar->addSeparator();
	singleViewAction->addTo(toolBar);
	splitViewAction->addTo(toolBar);
	fullViewAction->addTo(toolBar);

	// create config menu
	QPopupMenu* config = new QPopupMenu(this);
	menu->insertItem("&File", config);
	loadAction->addTo(config);
	saveAction->addTo(config);
	saveAsAction->addTo(config);
	config->insertSeparator();
	quitAction->addTo(config);

	// create edit menu
	QPopupMenu* editMenu = new QPopupMenu(this);
	menu->insertItem("&Edit", editMenu);
	searchAction->addTo(editMenu);

	// create options menu
	QPopupMenu* optionMenu = new QPopupMenu(this);
	menu->insertItem("&Option", optionMenu);
	showNameAction->addTo(optionMenu);
	showRangeAction->addTo(optionMenu);
	showDataAction->addTo(optionMenu);
	optionMenu->insertSeparator();
	showAllAction->addTo(optionMenu);
	showDebugAction->addTo(optionMenu);

	// create help menu
	QPopupMenu* helpMenu = new QPopupMenu(this);
	menu->insertSeparator();
	menu->insertItem("&Help", helpMenu);
	showIntroAction->addTo(helpMenu);
	showAboutAction->addTo(helpMenu);

	connect(configList, SIGNAL(menuChanged(struct menu *)),
		helpText, SLOT(setInfo(struct menu *)));
	connect(configList, SIGNAL(menuSelected(struct menu *)),
		SLOT(changeMenu(struct menu *)));
	connect(configList, SIGNAL(parentSelected()),
		SLOT(goBack()));
	connect(menuList, SIGNAL(menuChanged(struct menu *)),
		helpText, SLOT(setInfo(struct menu *)));
	connect(menuList, SIGNAL(menuSelected(struct menu *)),
		SLOT(changeMenu(struct menu *)));

	connect(configList, SIGNAL(gotFocus(struct menu *)),
		helpText, SLOT(setInfo(struct menu *)));
	connect(menuList, SIGNAL(gotFocus(struct menu *)),
		helpText, SLOT(setInfo(struct menu *)));
	connect(menuList, SIGNAL(gotFocus(struct menu *)),
		SLOT(listFocusChanged(void)));
	connect(helpText, SIGNAL(menuSelected(struct menu *)),
		SLOT(setMenuLink(struct menu *)));

	QString listMode = configSettings->readEntry("/listMode", "symbol");
	if (listMode == "single")
		showSingleView();
	else if (listMode == "full")
		showFullView();
	else /*if (listMode == "split")*/
		showSplitView();

	// UI setup done, restore splitter positions
	QValueList<int> sizes = configSettings->readSizes("/split1", &ok);
	if (ok)
		split1->setSizes(sizes);

	sizes = configSettings->readSizes("/split2", &ok);
	if (ok)
		split2->setSizes(sizes);
}

void ConfigMainWindow::loadConfig(void)
{
	QString s = QFileDialog::getOpenFileName(".config", NULL, this);
	if (s.isNull())
		return;
	if (conf_read(QFile::encodeName(s)))
		QMessageBox::information(this, "qconf", "Unable to load configuration!");
	ConfigView::updateListAll();
}

void ConfigMainWindow::saveConfig(void)
{
	if (conf_write(NULL))
		QMessageBox::information(this, "qconf", "Unable to save configuration!");
}

void ConfigMainWindow::saveConfigAs(void)
{
	QString s = QFileDialog::getSaveFileName(".config", NULL, this);
	if (s.isNull())
		return;
	if (conf_write(QFile::encodeName(s)))
		QMessageBox::information(this, "qconf", "Unable to save configuration!");
}

void ConfigMainWindow::searchConfig(void)
{
	if (!searchWindow)
		searchWindow = new ConfigSearchWindow(this, "search");
	searchWindow->show();
}

void ConfigMainWindow::changeMenu(struct menu *menu)
{
	configList->setRootMenu(menu);
	if (configList->rootEntry->parent == &rootmenu)
		backAction->setEnabled(FALSE);
	else
		backAction->setEnabled(TRUE);
}

void ConfigMainWindow::setMenuLink(struct menu *menu)
{
	struct menu *parent;
	ConfigList* list = NULL;
	ConfigItem* item;

	if (!menu_is_visible(menu) && !configView->showAll())
		return;

	switch (configList->mode) {
	case singleMode:
		list = configList;
		parent = menu_get_parent_menu(menu);
		if (!parent)
			return;
		list->setRootMenu(parent);
		break;
	case symbolMode:
		if (menu->flags & MENU_ROOT) {
			configList->setRootMenu(menu);
			configList->clearSelection();
			list = menuList;
		} else {
			list = configList;
			parent = menu_get_parent_menu(menu->parent);
			if (!parent)
				return;
			item = menuList->findConfigItem(parent);
			if (item) {
				menuList->setSelected(item, TRUE);
				menuList->ensureItemVisible(item);
			}
			list->setRootMenu(parent);
		}
		break;
	case fullMode:
		list = configList;
		break;
	}

	if (list) {
		item = list->findConfigItem(menu);
		if (item) {
			list->setSelected(item, TRUE);
			list->ensureItemVisible(item);
			list->setFocus();
		}
	}
}

void ConfigMainWindow::listFocusChanged(void)
{
	if (menuList->mode == menuMode)
		configList->clearSelection();
}

void ConfigMainWindow::goBack(void)
{
	ConfigItem* item;

	configList->setParentMenu();
	if (configList->rootEntry == &rootmenu)
		backAction->setEnabled(FALSE);
	item = (ConfigItem*)menuList->selectedItem();
	while (item) {
		if (item->menu == configList->rootEntry) {
			menuList->setSelected(item, TRUE);
			break;
		}
		item = (ConfigItem*)item->parent();
	}
}

void ConfigMainWindow::showSingleView(void)
{
	menuView->hide();
	menuList->setRootMenu(0);
	configList->mode = singleMode;
	if (configList->rootEntry == &rootmenu)
		configList->updateListAll();
	else
		configList->setRootMenu(&rootmenu);
	configList->setAllOpen(TRUE);
	configList->setFocus();
}

void ConfigMainWindow::showSplitView(void)
{
	configList->mode = symbolMode;
	if (configList->rootEntry == &rootmenu)
		configList->updateListAll();
	else
		configList->setRootMenu(&rootmenu);
	configList->setAllOpen(TRUE);
	configApp->processEvents();
	menuList->mode = menuMode;
	menuList->setRootMenu(&rootmenu);
	menuList->setAllOpen(TRUE);
	menuView->show();
	menuList->setFocus();
}

void ConfigMainWindow::showFullView(void)
{
	menuView->hide();
	menuList->setRootMenu(0);
	configList->mode = fullMode;
	if (configList->rootEntry == &rootmenu)
		configList->updateListAll();
	else
		configList->setRootMenu(&rootmenu);
	configList->setAllOpen(FALSE);
	configList->setFocus();
}

/*
 * ask for saving configuration before quitting
 * TODO ask only when something changed
 */
void ConfigMainWindow::closeEvent(QCloseEvent* e)
{
	if (!conf_get_changed()) {
		e->accept();
		return;
	}
	QMessageBox mb("qconf", "Save configuration?", QMessageBox::Warning,
			QMessageBox::Yes | QMessageBox::Default, QMessageBox::No, QMessageBox::Cancel | QMessageBox::Escape);
	mb.setButtonText(QMessageBox::Yes, "&Save Changes");
	mb.setButtonText(QMessageBox::No, "&Discard Changes");
	mb.setButtonText(QMessageBox::Cancel, "Cancel Exit");
	switch (mb.exec()) {
	case QMessageBox::Yes:
		conf_write(NULL);
	case QMessageBox::No:
		e->accept();
		break;
	case QMessageBox::Cancel:
		e->ignore();
		break;
	}
}

void ConfigMainWindow::showIntro(void)
{
	static char str[] = "Welcome to the qconf graphical kernel configuration tool for Linux.\n\n"
		"For each option, a blank box indicates the feature is disabled, a check\n"
		"indicates it is enabled, and a dot indicates that it is to be compiled\n"
		"as a module.  Clicking on the box will cycle through the three states.\n\n"
		"If you do not see an option (e.g., a device driver) that you believe\n"
		"should be present, try turning on Show All Options under the Options menu.\n"
		"Although there is no cross reference yet to help you figure out what other\n"
		"options must be enabled to support the option you are interested in, you can\n"
		"still view the help of a grayed-out option.\n\n"
		"Toggling Show Debug Info under the Options menu will show the dependencies,\n"
		"which you can then match by examining other options.\n\n";

	QMessageBox::information(this, "qconf", str);
}

void ConfigMainWindow::showAbout(void)
{
	static char str[] = "qconf is Copyright (C) 2002 Roman Zippel <zippel@linux-m68k.org>.\n\n"
		"Bug reports and feature request can also be entered at http://bugzilla.kernel.org/\n";

	QMessageBox::information(this, "qconf", str);
}

void ConfigMainWindow::saveSettings(void)
{
	configSettings->writeEntry("/window x", pos().x());
	configSettings->writeEntry("/window y", pos().y());
	configSettings->writeEntry("/window width", size().width());
	configSettings->writeEntry("/window height", size().height());

	QString entry;
	switch(configList->mode) {
	case singleMode :
		entry = "single";
		break;

	case symbolMode :
		entry = "split";
		break;

	case fullMode :
		entry = "full";
		break;
	}
	configSettings->writeEntry("/listMode", entry);

	configSettings->writeSizes("/split1", split1->sizes());
	configSettings->writeSizes("/split2", split2->sizes());
}

void ConfigMainWindow::conf_changed(void)
{
	if (saveAction)
		saveAction->setEnabled(conf_get_changed());
}

void fixup_rootmenu(struct menu *menu)
{
	struct menu *child;
	static int menu_cnt = 0;

	menu->flags |= MENU_ROOT;
	for (child = menu->list; child; child = child->next) {
		if (child->prompt && child->prompt->type == P_MENU) {
			menu_cnt++;
			fixup_rootmenu(child);
			menu_cnt--;
		} else if (!menu_cnt)
			fixup_rootmenu(child);
	}
}

static const char *progname;

static void usage(void)
{
	printf("%s <config>\n", progname);
	exit(0);
}

int main(int ac, char** av)
{
	ConfigMainWindow* v;
	const char *name;

	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

#ifndef LKC_DIRECT_LINK
	kconfig_load();
#endif

	progname = av[0];
	configApp = new QApplication(ac, av);
	if (ac > 1 && av[1][0] == '-') {
		switch (av[1][1]) {
		case 'h':
		case '?':
			usage();
		}
		name = av[2];
	} else
		name = av[1];
	if (!name)
		usage();

	conf_parse(name);
	fixup_rootmenu(&rootmenu);
	conf_read(NULL);
	//zconfdump(stdout);

	configSettings = new ConfigSettings();
	configSettings->beginGroup("/kconfig/qconf");
	v = new ConfigMainWindow();

	//zconfdump(stdout);
	configApp->setMainWidget(v);
	configApp->connect(configApp, SIGNAL(lastWindowClosed()), SLOT(quit()));
	configApp->connect(configApp, SIGNAL(aboutToQuit()), v, SLOT(saveSettings()));
	v->show();
	configApp->exec();

	configSettings->endGroup();
	delete configSettings;

	return 0;
}
