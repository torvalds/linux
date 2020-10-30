// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2002 Roman Zippel <zippel@linux-m68k.org>
 * Copyright (C) 2015 Boris Barbulovski <bbarbulovski@gmail.com>
 */

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDebug>
#include <QDesktopWidget>
#include <QFileDialog>
#include <QLabel>
#include <QLayout>
#include <QList>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QToolBar>

#include <stdlib.h>

#include "lkc.h"
#include "qconf.h"

#include "images.h"


static QApplication *configApp;
static ConfigSettings *configSettings;

QAction *ConfigMainWindow::saveAction;

ConfigSettings::ConfigSettings()
	: QSettings("kernel.org", "qconf")
{
}

/**
 * Reads a list of integer values from the application settings.
 */
QList<int> ConfigSettings::readSizes(const QString& key, bool *ok)
{
	QList<int> result;

	if (contains(key))
	{
		QStringList entryList = value(key).toStringList();
		QStringList::Iterator it;

		for (it = entryList.begin(); it != entryList.end(); ++it)
			result.push_back((*it).toInt());

		*ok = true;
	}
	else
		*ok = false;

	return result;
}

/**
 * Writes a list of integer values to the application settings.
 */
bool ConfigSettings::writeSizes(const QString& key, const QList<int>& value)
{
	QStringList stringList;
	QList<int>::ConstIterator it;

	for (it = value.begin(); it != value.end(); ++it)
		stringList.push_back(QString::number(*it));
	setValue(key, stringList);

	return true;
}

QIcon ConfigItem::symbolYesIcon;
QIcon ConfigItem::symbolModIcon;
QIcon ConfigItem::symbolNoIcon;
QIcon ConfigItem::choiceYesIcon;
QIcon ConfigItem::choiceNoIcon;
QIcon ConfigItem::menuIcon;
QIcon ConfigItem::menubackIcon;

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
		setIcon(promptColIdx, menubackIcon);
		prompt = "..";
		goto set_prompt;
	}

	sym = menu->sym;
	prop = menu->prompt;
	prompt = menu_get_prompt(menu);

	if (prop) switch (prop->type) {
	case P_MENU:
		if (list->mode == singleMode || list->mode == symbolMode) {
			/* a menuconfig entry is displayed differently
			 * depending whether it's at the view root or a child.
			 */
			if (sym && list->rootEntry == menu)
				break;
			setIcon(promptColIdx, menuIcon);
		} else {
			if (sym)
				break;
			setIcon(promptColIdx, QIcon());
		}
		goto set_prompt;
	case P_COMMENT:
		setIcon(promptColIdx, QIcon());
		goto set_prompt;
	default:
		;
	}
	if (!sym)
		goto set_prompt;

	setText(nameColIdx, sym->name);

	type = sym_get_type(sym);
	switch (type) {
	case S_BOOLEAN:
	case S_TRISTATE:
		char ch;

		if (!sym_is_changeable(sym) && list->optMode == normalOpt) {
			setIcon(promptColIdx, QIcon());
			break;
		}
		expr = sym_get_tristate_value(sym);
		switch (expr) {
		case yes:
			if (sym_is_choice_value(sym) && type == S_BOOLEAN)
				setIcon(promptColIdx, choiceYesIcon);
			else
				setIcon(promptColIdx, symbolYesIcon);
			ch = 'Y';
			break;
		case mod:
			setIcon(promptColIdx, symbolModIcon);
			ch = 'M';
			break;
		default:
			if (sym_is_choice_value(sym) && type == S_BOOLEAN)
				setIcon(promptColIdx, choiceNoIcon);
			else
				setIcon(promptColIdx, symbolNoIcon);
			ch = 'N';
			break;
		}

		setText(dataColIdx, QChar(ch));
		break;
	case S_INT:
	case S_HEX:
	case S_STRING:
		setText(dataColIdx, sym_get_string_value(sym));
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
			setExpanded(true);
		sym_calc_value(menu->sym);

		if (menu->sym) {
			enum symbol_type type = menu->sym->type;

			// Allow to edit "int", "hex", and "string" in-place in
			// the data column. Unfortunately, you cannot specify
			// the flags per column. Set ItemIsEditable for all
			// columns here, and check the column in createEditor().
			if (type == S_INT || type == S_HEX || type == S_STRING)
				setFlags(flags() | Qt::ItemIsEditable);
		}
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

QWidget *ConfigItemDelegate::createEditor(QWidget *parent,
					  const QStyleOptionViewItem &option,
					  const QModelIndex &index) const
{
	ConfigItem *item;

	// Only the data column is editable
	if (index.column() != dataColIdx)
		return nullptr;

	// You cannot edit invisible menus
	item = static_cast<ConfigItem *>(index.internalPointer());
	if (!item || !item->menu || !menu_is_visible(item->menu))
		return nullptr;

	return QStyledItemDelegate::createEditor(parent, option, index);
}

void ConfigItemDelegate::setModelData(QWidget *editor,
				      QAbstractItemModel *model,
				      const QModelIndex &index) const
{
	QLineEdit *lineEdit;
	ConfigItem *item;
	struct symbol *sym;
	bool success;

	lineEdit = qobject_cast<QLineEdit *>(editor);
	// If this is not a QLineEdit, use the parent's default.
	// (does this happen?)
	if (!lineEdit)
		goto parent;

	item = static_cast<ConfigItem *>(index.internalPointer());
	if (!item || !item->menu)
		goto parent;

	sym = item->menu->sym;
	if (!sym)
		goto parent;

	success = sym_set_string_value(sym, lineEdit->text().toUtf8().data());
	if (success) {
		ConfigList::updateListForAll();
	} else {
		QMessageBox::information(editor, "qconf",
			"Cannot set the data (maybe due to out of range).\n"
			"Setting the old value.");
		lineEdit->setText(sym_get_string_value(sym));
	}

parent:
	QStyledItemDelegate::setModelData(editor, model, index);
}

ConfigList::ConfigList(QWidget *parent, const char *name)
	: QTreeWidget(parent),
	  updateAll(false),
	  showName(false), mode(singleMode), optMode(normalOpt),
	  rootEntry(0), headerPopup(0)
{
	setObjectName(name);
	setSortingEnabled(false);
	setRootIsDecorated(true);

	setVerticalScrollMode(ScrollPerPixel);
	setHorizontalScrollMode(ScrollPerPixel);

	setHeaderLabels(QStringList() << "Option" << "Name" << "Value");

	connect(this, SIGNAL(itemSelectionChanged(void)),
		SLOT(updateSelection(void)));

	if (name) {
		configSettings->beginGroup(name);
		showName = configSettings->value("/showName", false).toBool();
		optMode = (enum optionMode)configSettings->value("/optionMode", 0).toInt();
		configSettings->endGroup();
		connect(configApp, SIGNAL(aboutToQuit()), SLOT(saveSettings()));
	}

	showColumn(promptColIdx);

	setItemDelegate(new ConfigItemDelegate(this));

	allLists.append(this);

	reinit();
}

ConfigList::~ConfigList()
{
	allLists.removeOne(this);
}

bool ConfigList::menuSkip(struct menu *menu)
{
	if (optMode == normalOpt && menu_is_visible(menu))
		return false;
	if (optMode == promptOpt && menu_has_prompt(menu))
		return false;
	if (optMode == allOpt)
		return false;
	return true;
}

void ConfigList::reinit(void)
{
	hideColumn(nameColIdx);

	if (showName)
		showColumn(nameColIdx);

	updateListAll();
}

void ConfigList::setOptionMode(QAction *action)
{
	if (action == showNormalAction)
		optMode = normalOpt;
	else if (action == showAllAction)
		optMode = allOpt;
	else
		optMode = promptOpt;

	updateListAll();
}

void ConfigList::saveSettings(void)
{
	if (!objectName().isEmpty()) {
		configSettings->beginGroup(objectName());
		configSettings->setValue("/showName", showName);
		configSettings->setValue("/optionMode", (int)optMode);
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

	if (selectedItems().count() == 0)
		return;

	ConfigItem* item = (ConfigItem*)selectedItems().first();
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

void ConfigList::updateList()
{
	ConfigItem* last = 0;
	ConfigItem *item;

	if (!rootEntry) {
		if (mode != listMode)
			goto update;
		QTreeWidgetItemIterator it(this);

		while (*it) {
			item = (ConfigItem*)(*it);
			if (!item->menu)
				continue;
			item->testUpdateMenu(menu_is_visible(item->menu));

			++it;
		}
		return;
	}

	if (rootEntry != &rootmenu && (mode == singleMode ||
	    (mode == symbolMode && rootEntry->parent != &rootmenu))) {
		item = (ConfigItem *)topLevelItem(0);
		if (!item)
			item = new ConfigItem(this, 0, true);
		last = item;
	}
	if ((mode == singleMode || (mode == symbolMode && !(rootEntry->flags & MENU_ROOT))) &&
	    rootEntry->sym && rootEntry->prompt) {
		item = last ? last->nextSibling() : nullptr;
		if (!item)
			item = new ConfigItem(this, last, rootEntry, true);
		else
			item->testUpdateMenu(true);

		updateMenuList(item, rootEntry);
		update();
		resizeColumnToContents(0);
		return;
	}
update:
	updateMenuList(rootEntry);
	update();
	resizeColumnToContents(0);
}

void ConfigList::updateListForAll()
{
	QListIterator<ConfigList *> it(allLists);

	while (it.hasNext()) {
		ConfigList *list = it.next();

		list->updateList();
	}
}

void ConfigList::updateListAllForAll()
{
	QListIterator<ConfigList *> it(allLists);

	while (it.hasNext()) {
		ConfigList *list = it.next();

		list->updateList();
	}
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
			item->setExpanded(true);
		ConfigList::updateListForAll();
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
			item->setExpanded(!item->isExpanded());
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
				item->setExpanded(!item->isExpanded());
			else if (oldexpr == no)
				item->setExpanded(true);
		}
		if (oldexpr != newexpr)
			ConfigList::updateListForAll();
		break;
	default:
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
	updateMenuList(0);
	rootEntry = menu;
	updateListAll();
	if (currentItem()) {
		setSelected(currentItem(), hasFocus());
		scrollToItem(currentItem());
	}
}

void ConfigList::setParentMenu(void)
{
	ConfigItem* item;
	struct menu *oldroot;

	oldroot = rootEntry;
	if (rootEntry == &rootmenu)
		return;
	setRootMenu(menu_get_parent_menu(rootEntry->parent));

	QTreeWidgetItemIterator it(this);
	while (*it) {
		item = (ConfigItem *)(*it);
		if (item->menu == oldroot) {
			setCurrentItem(item);
			scrollToItem(item);
			break;
		}

		++it;
	}
}

/*
 * update all the children of a menu entry
 *   removes/adds the entries from the parent widget as necessary
 *
 * parent: either the menu list widget or a menu entry widget
 * menu: entry to be updated
 */
void ConfigList::updateMenuList(ConfigItem *parent, struct menu* menu)
{
	struct menu* child;
	ConfigItem* item;
	ConfigItem* last;
	bool visible;
	enum prop_type type;

	if (!menu) {
		while (parent->childCount() > 0)
		{
			delete parent->takeChild(0);
		}

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
		if (!menuSkip(child)) {
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

void ConfigList::updateMenuList(struct menu *menu)
{
	struct menu* child;
	ConfigItem* item;
	ConfigItem* last;
	bool visible;
	enum prop_type type;

	if (!menu) {
		while (topLevelItemCount() > 0)
		{
			delete takeTopLevelItem(0);
		}

		return;
	}

	last = (ConfigItem *)topLevelItem(0);
	if (last && !last->goParent)
		last = 0;
	for (child = menu->list; child; child = child->next) {
		item = last ? last->nextSibling() : (ConfigItem *)topLevelItem(0);
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
		if (!menuSkip(child)) {
			if (!child->sym && !child->list && !child->prompt)
				continue;
			if (!item || item->menu != child)
				item = new ConfigItem(this, last, child, visible);
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
			last = (ConfigItem *)topLevelItem(0);
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
	QTreeWidgetItem* i = currentItem();
	ConfigItem* item;
	struct menu *menu;
	enum prop_type type;

	if (ev->key() == Qt::Key_Escape && mode != fullMode && mode != listMode) {
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
	case Qt::Key_Return:
	case Qt::Key_Enter:
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
			if (mode == menuMode)
				emit menuSelected(menu);
			else
				emit itemSelected(menu);
			break;
		}
	case Qt::Key_Space:
		changeValue(item);
		break;
	case Qt::Key_N:
		setValue(item, no);
		break;
	case Qt::Key_M:
		setValue(item, mod);
		break;
	case Qt::Key_Y:
		setValue(item, yes);
		break;
	default:
		Parent::keyPressEvent(ev);
		return;
	}
	ev->accept();
}

void ConfigList::mousePressEvent(QMouseEvent* e)
{
	//QPoint p(contentsToViewport(e->pos()));
	//printf("contentsMousePressEvent: %d,%d\n", p.x(), p.y());
	Parent::mousePressEvent(e);
}

void ConfigList::mouseReleaseEvent(QMouseEvent* e)
{
	QPoint p = e->pos();
	ConfigItem* item = (ConfigItem*)itemAt(p);
	struct menu *menu;
	enum prop_type ptype;
	QIcon icon;
	int idx, x;

	if (!item)
		goto skip;

	menu = item->menu;
	x = header()->offset() + p.x();
	idx = header()->logicalIndexAt(x);
	switch (idx) {
	case promptColIdx:
		icon = item->icon(promptColIdx);
		if (!icon.isNull()) {
			int off = header()->sectionPosition(0) + visualRect(indexAt(p)).x() + 4; // 4 is Hardcoded image offset. There might be a way to do it properly.
			if (x >= off && x < off + icon.availableSizes().first().width()) {
				if (item->goParent) {
					emit parentSelected();
					break;
				} else if (!menu)
					break;
				ptype = menu->prompt ? menu->prompt->type : P_UNKNOWN;
				if (ptype == P_MENU && rootEntry != menu &&
				    mode != fullMode && mode != menuMode &&
                                    mode != listMode)
					emit menuSelected(menu);
				else
					changeValue(item);
			}
		}
		break;
	case dataColIdx:
		changeValue(item);
		break;
	}

skip:
	//printf("contentsMouseReleaseEvent: %d,%d\n", p.x(), p.y());
	Parent::mouseReleaseEvent(e);
}

void ConfigList::mouseMoveEvent(QMouseEvent* e)
{
	//QPoint p(contentsToViewport(e->pos()));
	//printf("contentsMouseMoveEvent: %d,%d\n", p.x(), p.y());
	Parent::mouseMoveEvent(e);
}

void ConfigList::mouseDoubleClickEvent(QMouseEvent* e)
{
	QPoint p = e->pos();
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
	if (ptype == P_MENU && mode != listMode) {
		if (mode == singleMode)
			emit itemSelected(menu);
		else if (mode == symbolMode)
			emit menuSelected(menu);
	} else if (menu->sym)
		changeValue(item);

skip:
	//printf("contentsMouseDoubleClickEvent: %d,%d\n", p.x(), p.y());
	Parent::mouseDoubleClickEvent(e);
}

void ConfigList::focusInEvent(QFocusEvent *e)
{
	struct menu *menu = NULL;

	Parent::focusInEvent(e);

	ConfigItem* item = (ConfigItem *)currentItem();
	if (item) {
		setSelected(item, true);
		menu = item->menu;
	}
	emit gotFocus(menu);
}

void ConfigList::contextMenuEvent(QContextMenuEvent *e)
{
	if (!headerPopup) {
		QAction *action;

		headerPopup = new QMenu(this);
		action = new QAction("Show Name", this);
		action->setCheckable(true);
		connect(action, SIGNAL(toggled(bool)),
			SLOT(setShowName(bool)));
		connect(this, SIGNAL(showNameChanged(bool)),
			action, SLOT(setChecked(bool)));
		action->setChecked(showName);
		headerPopup->addAction(action);
	}

	headerPopup->exec(e->globalPos());
	e->accept();
}

void ConfigList::setShowName(bool on)
{
	if (showName == on)
		return;

	showName = on;
	reinit();
	emit showNameChanged(on);
}

QList<ConfigList *> ConfigList::allLists;
QAction *ConfigList::showNormalAction;
QAction *ConfigList::showAllAction;
QAction *ConfigList::showPromptAction;

void ConfigList::setAllOpen(bool open)
{
	QTreeWidgetItemIterator it(this);

	while (*it) {
		(*it)->setExpanded(open);

		++it;
	}
}

ConfigInfoView::ConfigInfoView(QWidget* parent, const char *name)
	: Parent(parent), sym(0), _menu(0)
{
	setObjectName(name);
	setOpenLinks(false);

	if (!objectName().isEmpty()) {
		configSettings->beginGroup(objectName());
		setShowDebug(configSettings->value("/showDebug", false).toBool());
		configSettings->endGroup();
		connect(configApp, SIGNAL(aboutToQuit()), SLOT(saveSettings()));
	}

	contextMenu = createStandardContextMenu();
	QAction *action = new QAction("Show Debug Info", contextMenu);

	action->setCheckable(true);
	connect(action, SIGNAL(toggled(bool)), SLOT(setShowDebug(bool)));
	connect(this, SIGNAL(showDebugChanged(bool)), action, SLOT(setChecked(bool)));
	action->setChecked(showDebug());
	contextMenu->addSeparator();
	contextMenu->addAction(action);
}

void ConfigInfoView::saveSettings(void)
{
	if (!objectName().isEmpty()) {
		configSettings->beginGroup(objectName());
		configSettings->setValue("/showDebug", showDebug());
		configSettings->endGroup();
	}
}

void ConfigInfoView::setShowDebug(bool b)
{
	if (_showDebug != b) {
		_showDebug = b;
		if (_menu)
			menuInfo();
		else if (sym)
			symbolInfo();
		emit showDebugChanged(b);
	}
}

void ConfigInfoView::setInfo(struct menu *m)
{
	if (_menu == m)
		return;
	_menu = m;
	sym = NULL;
	if (!_menu)
		clear();
	else
		menuInfo();
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
	QString info;
	QTextStream stream(&info);

	sym = _menu->sym;
	if (sym) {
		if (_menu->prompt) {
			stream << "<big><b>";
			stream << print_filter(_menu->prompt->text);
			stream << "</b></big>";
			if (sym->name) {
				stream << " (";
				if (showDebug())
					stream << "<a href=\"s" << sym->name << "\">";
				stream << print_filter(sym->name);
				if (showDebug())
					stream << "</a>";
				stream << ")";
			}
		} else if (sym->name) {
			stream << "<big><b>";
			if (showDebug())
				stream << "<a href=\"s" << sym->name << "\">";
			stream << print_filter(sym->name);
			if (showDebug())
				stream << "</a>";
			stream << "</b></big>";
		}
		stream << "<br><br>";

		if (showDebug())
			stream << debug_info(sym);

		struct gstr help_gstr = str_new();

		menu_get_ext_help(_menu, &help_gstr);
		stream << print_filter(str_get(&help_gstr));
		str_free(&help_gstr);
	} else if (_menu->prompt) {
		stream << "<big><b>";
		stream << print_filter(_menu->prompt->text);
		stream << "</b></big><br><br>";
		if (showDebug()) {
			if (_menu->prompt->visible.expr) {
				stream << "&nbsp;&nbsp;dep: ";
				expr_print(_menu->prompt->visible.expr,
					   expr_print_help, &stream, E_NONE);
				stream << "<br><br>";
			}

			stream << "defined at " << _menu->file->name << ":"
			       << _menu->lineno << "<br><br>";
		}
	}

	setText(info);
}

QString ConfigInfoView::debug_info(struct symbol *sym)
{
	QString debug;
	QTextStream stream(&debug);

	stream << "type: ";
	stream << print_filter(sym_type_name(sym->type));
	if (sym_is_choice(sym))
		stream << " (choice)";
	debug += "<br>";
	if (sym->rev_dep.expr) {
		stream << "reverse dep: ";
		expr_print(sym->rev_dep.expr, expr_print_help, &stream, E_NONE);
		stream << "<br>";
	}
	for (struct property *prop = sym->prop; prop; prop = prop->next) {
		switch (prop->type) {
		case P_PROMPT:
		case P_MENU:
			stream << "prompt: <a href=\"m" << sym->name << "\">";
			stream << print_filter(prop->text);
			stream << "</a><br>";
			break;
		case P_DEFAULT:
		case P_SELECT:
		case P_RANGE:
		case P_COMMENT:
		case P_IMPLY:
		case P_SYMBOL:
			stream << prop_get_type_name(prop->type);
			stream << ": ";
			expr_print(prop->expr, expr_print_help,
				   &stream, E_NONE);
			stream << "<br>";
			break;
		case P_CHOICE:
			if (sym_is_choice(sym)) {
				stream << "choice: ";
				expr_print(prop->expr, expr_print_help,
					   &stream, E_NONE);
				stream << "<br>";
			}
			break;
		default:
			stream << "unknown property: ";
			stream << prop_get_type_name(prop->type);
			stream << "<br>";
		}
		if (prop->visible.expr) {
			stream << "&nbsp;&nbsp;&nbsp;&nbsp;dep: ";
			expr_print(prop->visible.expr, expr_print_help,
				   &stream, E_NONE);
			stream << "<br>";
		}
	}
	stream << "<br>";

	return debug;
}

QString ConfigInfoView::print_filter(const QString &str)
{
	QRegExp re("[<>&\"\\n]");
	QString res = str;
	for (int i = 0; (i = res.indexOf(re, i)) >= 0;) {
		switch (res[i].toLatin1()) {
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
	QTextStream *stream = reinterpret_cast<QTextStream *>(data);

	if (sym && sym->name && !(sym->flags & SYMBOL_CONST)) {
		*stream << "<a href=\"s" << sym->name << "\">";
		*stream << print_filter(str);
		*stream << "</a>";
	} else {
		*stream << print_filter(str);
	}
}

void ConfigInfoView::clicked(const QUrl &url)
{
	QByteArray str = url.toEncoded();
	const std::size_t count = str.size();
	char *data = new char[count + 1];
	struct symbol **result;
	struct menu *m = NULL;

	if (count < 1) {
		delete[] data;
		return;
	}

	memcpy(data, str.constData(), count);
	data[count] = '\0';

	/* Seek for exact match */
	data[0] = '^';
	strcat(data, "$");
	result = sym_re_search(data);
	if (!result) {
		delete[] data;
		return;
	}

	sym = *result;

	/* Seek for the menu which holds the symbol */
	for (struct property *prop = sym->prop; prop; prop = prop->next) {
		    if (prop->type != P_PROMPT && prop->type != P_MENU)
			    continue;
		    m = prop->menu;
		    break;
	}

	if (!m) {
		/* Symbol is not visible as a menu */
		symbolInfo();
		emit showDebugChanged(true);
	} else {
		emit menuSelected(m);
	}

	free(result);
	delete[] data;
}

void ConfigInfoView::contextMenuEvent(QContextMenuEvent *event)
{
	contextMenu->popup(event->globalPos());
	event->accept();
}

ConfigSearchWindow::ConfigSearchWindow(ConfigMainWindow *parent)
	: Parent(parent), result(NULL)
{
	setObjectName("search");
	setWindowTitle("Search Config");

	QVBoxLayout* layout1 = new QVBoxLayout(this);
	layout1->setContentsMargins(11, 11, 11, 11);
	layout1->setSpacing(6);

	QHBoxLayout* layout2 = new QHBoxLayout();
	layout2->setContentsMargins(0, 0, 0, 0);
	layout2->setSpacing(6);
	layout2->addWidget(new QLabel("Find:", this));
	editField = new QLineEdit(this);
	connect(editField, SIGNAL(returnPressed()), SLOT(search()));
	layout2->addWidget(editField);
	searchButton = new QPushButton("Search", this);
	searchButton->setAutoDefault(false);
	connect(searchButton, SIGNAL(clicked()), SLOT(search()));
	layout2->addWidget(searchButton);
	layout1->addLayout(layout2);

	split = new QSplitter(this);
	split->setOrientation(Qt::Vertical);
	list = new ConfigList(split, "search");
	list->mode = listMode;
	info = new ConfigInfoView(split, "search");
	connect(list, SIGNAL(menuChanged(struct menu *)),
		info, SLOT(setInfo(struct menu *)));
	connect(list, SIGNAL(menuChanged(struct menu *)),
		parent, SLOT(setMenuLink(struct menu *)));

	layout1->addWidget(split);

	QVariant x, y;
	int width, height;
	bool ok;

	configSettings->beginGroup("search");
	width = configSettings->value("/window width", parent->width() / 2).toInt();
	height = configSettings->value("/window height", parent->height() / 2).toInt();
	resize(width, height);
	x = configSettings->value("/window x");
	y = configSettings->value("/window y");
	if (x.isValid() && y.isValid())
		move(x.toInt(), y.toInt());
	QList<int> sizes = configSettings->readSizes("/split", &ok);
	if (ok)
		split->setSizes(sizes);
	configSettings->endGroup();
	connect(configApp, SIGNAL(aboutToQuit()), SLOT(saveSettings()));
}

void ConfigSearchWindow::saveSettings(void)
{
	if (!objectName().isEmpty()) {
		configSettings->beginGroup(objectName());
		configSettings->setValue("/window x", pos().x());
		configSettings->setValue("/window y", pos().y());
		configSettings->setValue("/window width", size().width());
		configSettings->setValue("/window height", size().height());
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
	list->clear();
	info->clear();

	result = sym_re_search(editField->text().toLatin1());
	if (!result)
		return;
	for (p = result; *p; p++) {
		for_all_prompts((*p), prop)
			lastItem = new ConfigItem(list, lastItem, prop->menu,
						  menu_is_visible(prop->menu));
	}
}

/*
 * Construct the complete config widget
 */
ConfigMainWindow::ConfigMainWindow(void)
	: searchWindow(0)
{
	bool ok = true;
	QVariant x, y;
	int width, height;
	char title[256];

	QDesktopWidget *d = configApp->desktop();
	snprintf(title, sizeof(title), "%s%s",
		rootmenu.prompt->text,
		""
		);
	setWindowTitle(title);

	width = configSettings->value("/window width", d->width() - 64).toInt();
	height = configSettings->value("/window height", d->height() - 64).toInt();
	resize(width, height);
	x = configSettings->value("/window x");
	y = configSettings->value("/window y");
	if ((x.isValid())&&(y.isValid()))
		move(x.toInt(), y.toInt());

	// set up icons
	ConfigItem::symbolYesIcon = QIcon(QPixmap(xpm_symbol_yes));
	ConfigItem::symbolModIcon = QIcon(QPixmap(xpm_symbol_mod));
	ConfigItem::symbolNoIcon = QIcon(QPixmap(xpm_symbol_no));
	ConfigItem::choiceYesIcon = QIcon(QPixmap(xpm_choice_yes));
	ConfigItem::choiceNoIcon = QIcon(QPixmap(xpm_choice_no));
	ConfigItem::menuIcon = QIcon(QPixmap(xpm_menu));
	ConfigItem::menubackIcon = QIcon(QPixmap(xpm_menuback));

	QWidget *widget = new QWidget(this);
	QVBoxLayout *layout = new QVBoxLayout(widget);
	setCentralWidget(widget);

	split1 = new QSplitter(widget);
	split1->setOrientation(Qt::Horizontal);
	split1->setChildrenCollapsible(false);

	menuList = new ConfigList(widget, "menu");

	split2 = new QSplitter(widget);
	split2->setChildrenCollapsible(false);
	split2->setOrientation(Qt::Vertical);

	// create config tree
	configList = new ConfigList(widget, "config");

	helpText = new ConfigInfoView(widget, "help");

	layout->addWidget(split2);
	split2->addWidget(split1);
	split1->addWidget(configList);
	split1->addWidget(menuList);
	split2->addWidget(helpText);

	setTabOrder(configList, helpText);
	configList->setFocus();

	backAction = new QAction(QPixmap(xpm_back), "Back", this);
	connect(backAction, SIGNAL(triggered(bool)), SLOT(goBack()));

	QAction *quitAction = new QAction("&Quit", this);
	quitAction->setShortcut(Qt::CTRL + Qt::Key_Q);
	connect(quitAction, SIGNAL(triggered(bool)), SLOT(close()));

	QAction *loadAction = new QAction(QPixmap(xpm_load), "&Load", this);
	loadAction->setShortcut(Qt::CTRL + Qt::Key_L);
	connect(loadAction, SIGNAL(triggered(bool)), SLOT(loadConfig()));

	saveAction = new QAction(QPixmap(xpm_save), "&Save", this);
	saveAction->setShortcut(Qt::CTRL + Qt::Key_S);
	connect(saveAction, SIGNAL(triggered(bool)), SLOT(saveConfig()));

	conf_set_changed_callback(conf_changed);

	// Set saveAction's initial state
	conf_changed();
	configname = xstrdup(conf_get_configname());

	QAction *saveAsAction = new QAction("Save &As...", this);
	  connect(saveAsAction, SIGNAL(triggered(bool)), SLOT(saveConfigAs()));
	QAction *searchAction = new QAction("&Find", this);
	searchAction->setShortcut(Qt::CTRL + Qt::Key_F);
	  connect(searchAction, SIGNAL(triggered(bool)), SLOT(searchConfig()));
	singleViewAction = new QAction(QPixmap(xpm_single_view), "Single View", this);
	singleViewAction->setCheckable(true);
	  connect(singleViewAction, SIGNAL(triggered(bool)), SLOT(showSingleView()));
	splitViewAction = new QAction(QPixmap(xpm_split_view), "Split View", this);
	splitViewAction->setCheckable(true);
	  connect(splitViewAction, SIGNAL(triggered(bool)), SLOT(showSplitView()));
	fullViewAction = new QAction(QPixmap(xpm_tree_view), "Full View", this);
	fullViewAction->setCheckable(true);
	  connect(fullViewAction, SIGNAL(triggered(bool)), SLOT(showFullView()));

	QAction *showNameAction = new QAction("Show Name", this);
	  showNameAction->setCheckable(true);
	connect(showNameAction, SIGNAL(toggled(bool)), configList, SLOT(setShowName(bool)));
	showNameAction->setChecked(configList->showName);

	QActionGroup *optGroup = new QActionGroup(this);
	optGroup->setExclusive(true);
	connect(optGroup, SIGNAL(triggered(QAction*)), configList,
		SLOT(setOptionMode(QAction *)));
	connect(optGroup, SIGNAL(triggered(QAction *)), menuList,
		SLOT(setOptionMode(QAction *)));

	ConfigList::showNormalAction = new QAction("Show Normal Options", optGroup);
	ConfigList::showNormalAction->setCheckable(true);
	ConfigList::showAllAction = new QAction("Show All Options", optGroup);
	ConfigList::showAllAction->setCheckable(true);
	ConfigList::showPromptAction = new QAction("Show Prompt Options", optGroup);
	ConfigList::showPromptAction->setCheckable(true);

	QAction *showDebugAction = new QAction("Show Debug Info", this);
	  showDebugAction->setCheckable(true);
	  connect(showDebugAction, SIGNAL(toggled(bool)), helpText, SLOT(setShowDebug(bool)));
	  showDebugAction->setChecked(helpText->showDebug());

	QAction *showIntroAction = new QAction("Introduction", this);
	  connect(showIntroAction, SIGNAL(triggered(bool)), SLOT(showIntro()));
	QAction *showAboutAction = new QAction("About", this);
	  connect(showAboutAction, SIGNAL(triggered(bool)), SLOT(showAbout()));

	// init tool bar
	QToolBar *toolBar = addToolBar("Tools");
	toolBar->addAction(backAction);
	toolBar->addSeparator();
	toolBar->addAction(loadAction);
	toolBar->addAction(saveAction);
	toolBar->addSeparator();
	toolBar->addAction(singleViewAction);
	toolBar->addAction(splitViewAction);
	toolBar->addAction(fullViewAction);

	// create file menu
	QMenu *menu = menuBar()->addMenu("&File");
	menu->addAction(loadAction);
	menu->addAction(saveAction);
	menu->addAction(saveAsAction);
	menu->addSeparator();
	menu->addAction(quitAction);

	// create edit menu
	menu = menuBar()->addMenu("&Edit");
	menu->addAction(searchAction);

	// create options menu
	menu = menuBar()->addMenu("&Option");
	menu->addAction(showNameAction);
	menu->addSeparator();
	menu->addActions(optGroup->actions());
	menu->addSeparator();
	menu->addAction(showDebugAction);

	// create help menu
	menu = menuBar()->addMenu("&Help");
	menu->addAction(showIntroAction);
	menu->addAction(showAboutAction);

	connect (helpText, SIGNAL (anchorClicked (const QUrl &)),
		 helpText, SLOT (clicked (const QUrl &)) );

	connect(configList, SIGNAL(menuChanged(struct menu *)),
		helpText, SLOT(setInfo(struct menu *)));
	connect(configList, SIGNAL(menuSelected(struct menu *)),
		SLOT(changeMenu(struct menu *)));
	connect(configList, SIGNAL(itemSelected(struct menu *)),
		SLOT(changeItens(struct menu *)));
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

	QString listMode = configSettings->value("/listMode", "symbol").toString();
	if (listMode == "single")
		showSingleView();
	else if (listMode == "full")
		showFullView();
	else /*if (listMode == "split")*/
		showSplitView();

	// UI setup done, restore splitter positions
	QList<int> sizes = configSettings->readSizes("/split1", &ok);
	if (ok)
		split1->setSizes(sizes);

	sizes = configSettings->readSizes("/split2", &ok);
	if (ok)
		split2->setSizes(sizes);
}

void ConfigMainWindow::loadConfig(void)
{
	QString str;
	QByteArray ba;
	const char *name;

	str = QFileDialog::getOpenFileName(this, "", configname);
	if (str.isNull())
		return;

	ba = str.toLocal8Bit();
	name = ba.data();

	if (conf_read(name))
		QMessageBox::information(this, "qconf", "Unable to load configuration!");

	free(configname);
	configname = xstrdup(name);

	ConfigList::updateListAllForAll();
}

bool ConfigMainWindow::saveConfig(void)
{
	if (conf_write(configname)) {
		QMessageBox::information(this, "qconf", "Unable to save configuration!");
		return false;
	}
	conf_write_autoconf(0);

	return true;
}

void ConfigMainWindow::saveConfigAs(void)
{
	QString str;
	QByteArray ba;
	const char *name;

	str = QFileDialog::getSaveFileName(this, "", configname);
	if (str.isNull())
		return;

	ba = str.toLocal8Bit();
	name = ba.data();

	if (conf_write(name)) {
		QMessageBox::information(this, "qconf", "Unable to save configuration!");
	}
	conf_write_autoconf(0);

	free(configname);
	configname = xstrdup(name);
}

void ConfigMainWindow::searchConfig(void)
{
	if (!searchWindow)
		searchWindow = new ConfigSearchWindow(this);
	searchWindow->show();
}

void ConfigMainWindow::changeItens(struct menu *menu)
{
	configList->setRootMenu(menu);
}

void ConfigMainWindow::changeMenu(struct menu *menu)
{
	menuList->setRootMenu(menu);
}

void ConfigMainWindow::setMenuLink(struct menu *menu)
{
	struct menu *parent;
	ConfigList* list = NULL;
	ConfigItem* item;

	if (configList->menuSkip(menu))
		return;

	switch (configList->mode) {
	case singleMode:
		list = configList;
		parent = menu_get_parent_menu(menu);
		if (!parent)
			return;
		list->setRootMenu(parent);
		break;
	case menuMode:
		if (menu->flags & MENU_ROOT) {
			menuList->setRootMenu(menu);
			configList->clearSelection();
			list = configList;
		} else {
			parent = menu_get_parent_menu(menu->parent);
			if (!parent)
				return;

			/* Select the config view */
			item = configList->findConfigItem(parent);
			if (item) {
				configList->setSelected(item, true);
				configList->scrollToItem(item);
			}

			menuList->setRootMenu(parent);
			menuList->clearSelection();
			list = menuList;
		}
		break;
	case fullMode:
		list = configList;
		break;
	default:
		break;
	}

	if (list) {
		item = list->findConfigItem(menu);
		if (item) {
			list->setSelected(item, true);
			list->scrollToItem(item);
			list->setFocus();
			helpText->setInfo(menu);
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
	if (configList->rootEntry == &rootmenu)
		return;

	configList->setParentMenu();
}

void ConfigMainWindow::showSingleView(void)
{
	singleViewAction->setEnabled(false);
	singleViewAction->setChecked(true);
	splitViewAction->setEnabled(true);
	splitViewAction->setChecked(false);
	fullViewAction->setEnabled(true);
	fullViewAction->setChecked(false);

	backAction->setEnabled(true);

	menuList->hide();
	menuList->setRootMenu(0);
	configList->mode = singleMode;
	if (configList->rootEntry == &rootmenu)
		configList->updateListAll();
	else
		configList->setRootMenu(&rootmenu);
	configList->setFocus();
}

void ConfigMainWindow::showSplitView(void)
{
	singleViewAction->setEnabled(true);
	singleViewAction->setChecked(false);
	splitViewAction->setEnabled(false);
	splitViewAction->setChecked(true);
	fullViewAction->setEnabled(true);
	fullViewAction->setChecked(false);

	backAction->setEnabled(false);

	configList->mode = menuMode;
	if (configList->rootEntry == &rootmenu)
		configList->updateListAll();
	else
		configList->setRootMenu(&rootmenu);
	configList->setAllOpen(true);
	configApp->processEvents();
	menuList->mode = symbolMode;
	menuList->setRootMenu(&rootmenu);
	menuList->setAllOpen(true);
	menuList->show();
	menuList->setFocus();
}

void ConfigMainWindow::showFullView(void)
{
	singleViewAction->setEnabled(true);
	singleViewAction->setChecked(false);
	splitViewAction->setEnabled(true);
	splitViewAction->setChecked(false);
	fullViewAction->setEnabled(false);
	fullViewAction->setChecked(true);

	backAction->setEnabled(false);

	menuList->hide();
	menuList->setRootMenu(0);
	configList->mode = fullMode;
	if (configList->rootEntry == &rootmenu)
		configList->updateListAll();
	else
		configList->setRootMenu(&rootmenu);
	configList->setFocus();
}

/*
 * ask for saving configuration before quitting
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
		if (saveConfig())
			e->accept();
		else
			e->ignore();
		break;
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
	static const QString str =
		"Welcome to the qconf graphical configuration tool.\n"
		"\n"
		"For bool and tristate options, a blank box indicates the "
		"feature is disabled, a check indicates it is enabled, and a "
		"dot indicates that it is to be compiled as a module. Clicking "
		"on the box will cycle through the three states. For int, hex, "
		"and string options, double-clicking or pressing F2 on the "
		"Value cell will allow you to edit the value.\n"
		"\n"
		"If you do not see an option (e.g., a device driver) that you "
		"believe should be present, try turning on Show All Options "
		"under the Options menu. Enabling Show Debug Info will help you"
		"figure out what other options must be enabled to support the "
		"option you are interested in, and hyperlinks will navigate to "
		"them.\n"
		"\n"
		"Toggling Show Debug Info under the Options menu will show the "
		"dependencies, which you can then match by examining other "
		"options.\n";

	QMessageBox::information(this, "qconf", str);
}

void ConfigMainWindow::showAbout(void)
{
	static const QString str = "qconf is Copyright (C) 2002 Roman Zippel <zippel@linux-m68k.org>.\n"
		"Copyright (C) 2015 Boris Barbulovski <bbarbulovski@gmail.com>.\n\n"
		"Bug reports and feature request can also be entered at http://bugzilla.kernel.org/\n";

	QMessageBox::information(this, "qconf", str);
}

void ConfigMainWindow::saveSettings(void)
{
	configSettings->setValue("/window x", pos().x());
	configSettings->setValue("/window y", pos().y());
	configSettings->setValue("/window width", size().width());
	configSettings->setValue("/window height", size().height());

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

	default:
		break;
	}
	configSettings->setValue("/listMode", entry);

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
	printf("%s [-s] <config>\n", progname);
	exit(0);
}

int main(int ac, char** av)
{
	ConfigMainWindow* v;
	const char *name;

	progname = av[0];
	if (ac > 1 && av[1][0] == '-') {
		switch (av[1][1]) {
		case 's':
			conf_set_message_callback(NULL);
			break;
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

	configApp = new QApplication(ac, av);

	configSettings = new ConfigSettings();
	configSettings->beginGroup("/kconfig/qconf");
	v = new ConfigMainWindow();

	//zconfdump(stdout);
	configApp->connect(configApp, SIGNAL(lastWindowClosed()), SLOT(quit()));
	configApp->connect(configApp, SIGNAL(aboutToQuit()), v, SLOT(saveSettings()));
	v->show();
	configApp->exec();

	configSettings->endGroup();
	delete configSettings;
	delete v;
	delete configApp;

	return 0;
}
