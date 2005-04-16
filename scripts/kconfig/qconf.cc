/*
 * Copyright (C) 2002 Roman Zippel <zippel@linux-m68k.org>
 * Released under the terms of the GNU GPL v2.0.
 */

#include <qapplication.h>
#include <qmainwindow.h>
#include <qtoolbar.h>
#include <qvbox.h>
#include <qsplitter.h>
#include <qlistview.h>
#include <qtextview.h>
#include <qlineedit.h>
#include <qmenubar.h>
#include <qmessagebox.h>
#include <qaction.h>
#include <qheader.h>
#include <qfiledialog.h>
#include <qregexp.h>

#include <stdlib.h>

#include "lkc.h"
#include "qconf.h"

#include "qconf.moc"
#include "images.c"

static QApplication *configApp;

ConfigSettings::ConfigSettings()
	: showAll(false), showName(false), showRange(false), showData(false)
{
}

#if QT_VERSION >= 300
/**
 * Reads the list column settings from the application settings.
 */
void ConfigSettings::readListSettings()
{
	showAll = readBoolEntry("/kconfig/qconf/showAll", false);
	showName = readBoolEntry("/kconfig/qconf/showName", false);
	showRange = readBoolEntry("/kconfig/qconf/showRange", false);
	showData = readBoolEntry("/kconfig/qconf/showData", false);
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
#endif


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

#if QT_VERSION >= 300
/*
 * set the new data
 * TODO check the value
 */
void ConfigItem::okRename(int col)
{
	Parent::okRename(col);
	sym_set_string_value(menu->sym, text(dataColIdx).latin1());
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
	prompt = menu_get_prompt(menu);

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

	setText(nameColIdx, sym->name);

	type = sym_get_type(sym);
	switch (type) {
	case S_BOOLEAN:
	case S_TRISTATE:
		char ch;

		if (!sym_is_changable(sym) && !list->showAll) {
			setPixmap(promptColIdx, 0);
			setText(noColIdx, 0);
			setText(modColIdx, 0);
			setText(yesColIdx, 0);
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
			prompt.sprintf("%s: %s", prompt.latin1(), data);
		else
			prompt.sprintf("(%s) %s", data, prompt.latin1());
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

void ConfigLineEdit::show(ConfigItem* i)
{
	item = i;
	if (sym_get_string_value(item->menu->sym))
		setText(sym_get_string_value(item->menu->sym));
	else
		setText(0);
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

ConfigList::ConfigList(ConfigView* p, ConfigMainWindow* cv, ConfigSettings* configSettings)
	: Parent(p), cview(cv),
	  updateAll(false),
	  symbolYesPix(xpm_symbol_yes), symbolModPix(xpm_symbol_mod), symbolNoPix(xpm_symbol_no),
	  choiceYesPix(xpm_choice_yes), choiceNoPix(xpm_choice_no),
	  menuPix(xpm_menu), menuInvPix(xpm_menu_inv), menuBackPix(xpm_menuback), voidPix(xpm_void),
	  showAll(false), showName(false), showRange(false), showData(false),
	  rootEntry(0)
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

	if (configSettings) {
		showAll = configSettings->showAll;
		showName = configSettings->showName;
		showRange = configSettings->showRange;
		showData = configSettings->showData;
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

void ConfigList::updateSelection(void)
{
	struct menu *menu;
	enum prop_type type;

	ConfigItem* item = (ConfigItem*)selectedItem();
	if (!item)
		return;

	cview->setHelp(item);

	menu = item->menu;
	if (!menu)
		return;
	type = menu->prompt ? menu->prompt->type : P_UNKNOWN;
	if (mode == menuMode && type == P_MENU)
		emit menuSelected(menu);
}

void ConfigList::updateList(ConfigItem* item)
{
	ConfigItem* last = 0;

	if (!rootEntry)
		goto update;

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

void ConfigList::setAllOpen(bool open)
{
	QListViewItemIterator it(this);

	for (; it.current(); it++)
		it.current()->setOpen(open);
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

void ConfigList::keyPressEvent(QKeyEvent* ev)
{
	QListViewItem* i = currentItem();
	ConfigItem* item;
	struct menu *menu;
	enum prop_type type;

	if (ev->key() == Key_Escape && mode != fullMode) {
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
	Parent::focusInEvent(e);

	QListViewItem* item = currentItem();
	if (!item)
		return;

	setSelected(item, TRUE);
	emit gotFocus();
}

ConfigView* ConfigView::viewList;

ConfigView::ConfigView(QWidget* parent, ConfigMainWindow* cview,
		       ConfigSettings *configSettings)
	: Parent(parent)
{
	list = new ConfigList(this, cview, configSettings);
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

/*
 * Construct the complete config widget
 */
ConfigMainWindow::ConfigMainWindow(void)
{
	QMenuBar* menu;
	bool ok;
	int x, y, width, height;

	QWidget *d = configApp->desktop();

	ConfigSettings* configSettings = new ConfigSettings();
#if QT_VERSION >= 300
	width = configSettings->readNumEntry("/kconfig/qconf/window width", d->width() - 64);
	height = configSettings->readNumEntry("/kconfig/qconf/window height", d->height() - 64);
	resize(width, height);
	x = configSettings->readNumEntry("/kconfig/qconf/window x", 0, &ok);
	if (ok)
		y = configSettings->readNumEntry("/kconfig/qconf/window y", 0, &ok);
	if (ok)
		move(x, y);
	showDebug = configSettings->readBoolEntry("/kconfig/qconf/showDebug", false);

	// read list settings into configSettings, will be used later for ConfigList setup
	configSettings->readListSettings();
#else
	width = d->width() - 64;
	height = d->height() - 64;
	resize(width, height);
	showDebug = false;
#endif

	split1 = new QSplitter(this);
	split1->setOrientation(QSplitter::Horizontal);
	setCentralWidget(split1);

	menuView = new ConfigView(split1, this, configSettings);
	menuList = menuView->list;

	split2 = new QSplitter(split1);
	split2->setOrientation(QSplitter::Vertical);

	// create config tree
	configView = new ConfigView(split2, this, configSettings);
	configList = configView->list;

	helpText = new QTextView(split2);
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
	QAction *saveAction = new QAction("Save", QPixmap(xpm_save), "&Save", CTRL+Key_S, this);
	  connect(saveAction, SIGNAL(activated()), SLOT(saveConfig()));
	QAction *saveAsAction = new QAction("Save As...", "Save &As...", 0, this);
	  connect(saveAsAction, SIGNAL(activated()), SLOT(saveConfigAs()));
	QAction *singleViewAction = new QAction("Single View", QPixmap(xpm_single_view), "Split View", 0, this);
	  connect(singleViewAction, SIGNAL(activated()), SLOT(showSingleView()));
	QAction *splitViewAction = new QAction("Split View", QPixmap(xpm_split_view), "Split View", 0, this);
	  connect(splitViewAction, SIGNAL(activated()), SLOT(showSplitView()));
	QAction *fullViewAction = new QAction("Full View", QPixmap(xpm_tree_view), "Full View", 0, this);
	  connect(fullViewAction, SIGNAL(activated()), SLOT(showFullView()));

	QAction *showNameAction = new QAction(NULL, "Show Name", 0, this);
	  showNameAction->setToggleAction(TRUE);
	  showNameAction->setOn(configList->showName);
	  connect(showNameAction, SIGNAL(toggled(bool)), SLOT(setShowName(bool)));
	QAction *showRangeAction = new QAction(NULL, "Show Range", 0, this);
	  showRangeAction->setToggleAction(TRUE);
	  showRangeAction->setOn(configList->showRange);
	  connect(showRangeAction, SIGNAL(toggled(bool)), SLOT(setShowRange(bool)));
	QAction *showDataAction = new QAction(NULL, "Show Data", 0, this);
	  showDataAction->setToggleAction(TRUE);
	  showDataAction->setOn(configList->showData);
	  connect(showDataAction, SIGNAL(toggled(bool)), SLOT(setShowData(bool)));
	QAction *showAllAction = new QAction(NULL, "Show All Options", 0, this);
	  showAllAction->setToggleAction(TRUE);
	  showAllAction->setOn(configList->showAll);
	  connect(showAllAction, SIGNAL(toggled(bool)), SLOT(setShowAll(bool)));
	QAction *showDebugAction = new QAction(NULL, "Show Debug Info", 0, this);
	  showDebugAction->setToggleAction(TRUE);
	  showDebugAction->setOn(showDebug);
	  connect(showDebugAction, SIGNAL(toggled(bool)), SLOT(setShowDebug(bool)));

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

	connect(configList, SIGNAL(menuSelected(struct menu *)),
		SLOT(changeMenu(struct menu *)));
	connect(configList, SIGNAL(parentSelected()),
		SLOT(goBack()));
	connect(menuList, SIGNAL(menuSelected(struct menu *)),
		SLOT(changeMenu(struct menu *)));

	connect(configList, SIGNAL(gotFocus(void)),
		SLOT(listFocusChanged(void)));
	connect(menuList, SIGNAL(gotFocus(void)),
		SLOT(listFocusChanged(void)));

#if QT_VERSION >= 300
	QString listMode = configSettings->readEntry("/kconfig/qconf/listMode", "symbol");
	if (listMode == "single")
		showSingleView();
	else if (listMode == "full")
		showFullView();
	else /*if (listMode == "split")*/
		showSplitView();

	// UI setup done, restore splitter positions
	QValueList<int> sizes = configSettings->readSizes("/kconfig/qconf/split1", &ok);
	if (ok)
		split1->setSizes(sizes);

	sizes = configSettings->readSizes("/kconfig/qconf/split2", &ok);
	if (ok)
		split2->setSizes(sizes);
#else
	showSplitView();
#endif
	delete configSettings;
}

static QString print_filter(const char *str)
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

static void expr_print_help(void *data, const char *str)
{
	((QString*)data)->append(print_filter(str));
}

/*
 * display a new help entry as soon as a new menu entry is selected
 */
void ConfigMainWindow::setHelp(QListViewItem* item)
{
	struct symbol* sym;
	struct menu* menu = 0;

	configList->parent()->lineEdit->hide();
	if (item)
		menu = ((ConfigItem*)item)->menu;
	if (!menu) {
		helpText->setText(NULL);
		return;
	}

	QString head, debug, help;
	menu = ((ConfigItem*)item)->menu;
	sym = menu->sym;
	if (sym) {
		if (menu->prompt) {
			head += "<big><b>";
			head += print_filter(menu->prompt->text);
			head += "</b></big>";
			if (sym->name) {
				head += " (";
				head += print_filter(sym->name);
				head += ")";
			}
		} else if (sym->name) {
			head += "<big><b>";
			head += print_filter(sym->name);
			head += "</b></big>";
		}
		head += "<br><br>";

		if (showDebug) {
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
					debug += "prompt: ";
					debug += print_filter(prop->text);
					debug += "<br>";
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
		}

		help = print_filter(sym->help);
	} else if (menu->prompt) {
		head += "<big><b>";
		head += print_filter(menu->prompt->text);
		head += "</b></big><br><br>";
		if (showDebug) {
			if (menu->prompt->visible.expr) {
				debug += "&nbsp;&nbsp;dep: ";
				expr_print(menu->prompt->visible.expr, expr_print_help, &debug, E_NONE);
				debug += "<br><br>";
			}
		}
	}
	if (showDebug)
		debug += QString().sprintf("defined at %s:%d<br><br>", menu->file->name, menu->lineno);
	helpText->setText(head + debug + help);
}

void ConfigMainWindow::loadConfig(void)
{
	QString s = QFileDialog::getOpenFileName(".config", NULL, this);
	if (s.isNull())
		return;
	if (conf_read(s.latin1()))
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
	if (conf_write(s.latin1()))
		QMessageBox::information(this, "qconf", "Unable to save configuration!");
}

void ConfigMainWindow::changeMenu(struct menu *menu)
{
	configList->setRootMenu(menu);
	backAction->setEnabled(TRUE);
}

void ConfigMainWindow::listFocusChanged(void)
{
	if (menuList->hasFocus()) {
		if (menuList->mode == menuMode)
			configList->clearSelection();
		setHelp(menuList->selectedItem());
	} else if (configList->hasFocus()) {
		setHelp(configList->selectedItem());
	}
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

void ConfigMainWindow::setShowAll(bool b)
{
	if (configList->showAll == b)
		return;
	configList->showAll = b;
	configList->updateListAll();
	menuList->showAll = b;
	menuList->updateListAll();
}

void ConfigMainWindow::setShowDebug(bool b)
{
	if (showDebug == b)
		return;
	showDebug = b;
}

void ConfigMainWindow::setShowName(bool b)
{
	if (configList->showName == b)
		return;
	configList->showName = b;
	configList->reinit();
	menuList->showName = b;
	menuList->reinit();
}

void ConfigMainWindow::setShowRange(bool b)
{
	if (configList->showRange == b)
		return;
	configList->showRange = b;
	configList->reinit();
	menuList->showRange = b;
	menuList->reinit();
}

void ConfigMainWindow::setShowData(bool b)
{
	if (configList->showData == b)
		return;
	configList->showData = b;
	configList->reinit();
	menuList->showData = b;
	menuList->reinit();
}

/*
 * ask for saving configuration before quitting
 * TODO ask only when something changed
 */
void ConfigMainWindow::closeEvent(QCloseEvent* e)
{
	if (!sym_change_count) {
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
#if QT_VERSION >= 300
	ConfigSettings *configSettings = new ConfigSettings;
	configSettings->writeEntry("/kconfig/qconf/window x", pos().x());
	configSettings->writeEntry("/kconfig/qconf/window y", pos().y());
	configSettings->writeEntry("/kconfig/qconf/window width", size().width());
	configSettings->writeEntry("/kconfig/qconf/window height", size().height());
	configSettings->writeEntry("/kconfig/qconf/showName", configList->showName);
	configSettings->writeEntry("/kconfig/qconf/showRange", configList->showRange);
	configSettings->writeEntry("/kconfig/qconf/showData", configList->showData);
	configSettings->writeEntry("/kconfig/qconf/showAll", configList->showAll);
	configSettings->writeEntry("/kconfig/qconf/showDebug", showDebug);

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
	configSettings->writeEntry("/kconfig/qconf/listMode", entry);

	configSettings->writeSizes("/kconfig/qconf/split1", split1->sizes());
	configSettings->writeSizes("/kconfig/qconf/split2", split2->sizes());

	delete configSettings;
#endif
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

	v = new ConfigMainWindow();

	//zconfdump(stdout);
	v->show();
	configApp->connect(configApp, SIGNAL(lastWindowClosed()), SLOT(quit()));
	configApp->connect(configApp, SIGNAL(aboutToQuit()), v, SLOT(saveSettings()));
	configApp->exec();

	return 0;
}
