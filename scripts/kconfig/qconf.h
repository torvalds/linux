/*
 * Copyright (C) 2002 Roman Zippel <zippel@linux-m68k.org>
 * Released under the terms of the GNU GPL v2.0.
 */

#if QT_VERSION < 0x040000
#include <qlistview.h>
#else
#include <q3listview.h>
#endif
#include <qsettings.h>

#if QT_VERSION < 0x040000
#define Q3ValueList             QValueList
#define Q3PopupMenu             QPopupMenu
#define Q3ListView              QListView
#define Q3ListViewItem          QListViewItem
#define Q3VBox                  QVBox
#define Q3TextBrowser           QTextBrowser
#define Q3MainWindow            QMainWindow
#define Q3Action                QAction
#define Q3ToolBar               QToolBar
#define Q3ListViewItemIterator  QListViewItemIterator
#define Q3FileDialog            QFileDialog
#endif

class ConfigView;
class ConfigList;
class ConfigItem;
class ConfigLineEdit;
class ConfigMainWindow;

class ConfigSettings : public QSettings {
public:
	Q3ValueList<int> readSizes(const QString& key, bool *ok);
	bool writeSizes(const QString& key, const Q3ValueList<int>& value);
};

enum colIdx {
	promptColIdx, nameColIdx, noColIdx, modColIdx, yesColIdx, dataColIdx, colNr
};
enum listMode {
	singleMode, menuMode, symbolMode, fullMode, listMode
};
enum optionMode {
	normalOpt = 0, allOpt, promptOpt
};

class ConfigList : public Q3ListView {
	Q_OBJECT
	typedef class Q3ListView Parent;
public:
	ConfigList(ConfigView* p, const char *name = 0);
	void reinit(void);
	ConfigView* parent(void) const
	{
		return (ConfigView*)Parent::parent();
	}
	ConfigItem* findConfigItem(struct menu *);

protected:
	void keyPressEvent(QKeyEvent *e);
	void contentsMousePressEvent(QMouseEvent *e);
	void contentsMouseReleaseEvent(QMouseEvent *e);
	void contentsMouseMoveEvent(QMouseEvent *e);
	void contentsMouseDoubleClickEvent(QMouseEvent *e);
	void focusInEvent(QFocusEvent *e);
	void contextMenuEvent(QContextMenuEvent *e);

public slots:
	void setRootMenu(struct menu *menu);

	void updateList(ConfigItem *item);
	void setValue(ConfigItem* item, tristate val);
	void changeValue(ConfigItem* item);
	void updateSelection(void);
	void saveSettings(void);
signals:
	void menuChanged(struct menu *menu);
	void menuSelected(struct menu *menu);
	void parentSelected(void);
	void gotFocus(struct menu *);

public:
	void updateListAll(void)
	{
		updateAll = true;
		updateList(NULL);
		updateAll = false;
	}
	ConfigList* listView()
	{
		return this;
	}
	ConfigItem* firstChild() const
	{
		return (ConfigItem *)Parent::firstChild();
	}
	int mapIdx(colIdx idx)
	{
		return colMap[idx];
	}
	void addColumn(colIdx idx, const QString& label)
	{
		colMap[idx] = Parent::addColumn(label);
		colRevMap[colMap[idx]] = idx;
	}
	void removeColumn(colIdx idx)
	{
		int col = colMap[idx];
		if (col >= 0) {
			Parent::removeColumn(col);
			colRevMap[col] = colMap[idx] = -1;
		}
	}
	void setAllOpen(bool open);
	void setParentMenu(void);

	bool menuSkip(struct menu *);

	template <class P>
	void updateMenuList(P*, struct menu*);

	bool updateAll;

	QPixmap symbolYesPix, symbolModPix, symbolNoPix;
	QPixmap choiceYesPix, choiceNoPix;
	QPixmap menuPix, menuInvPix, menuBackPix, voidPix;

	bool showName, showRange, showData;
	enum listMode mode;
	enum optionMode optMode;
	struct menu *rootEntry;
	QColorGroup disabledColorGroup;
	QColorGroup inactivedColorGroup;
	Q3PopupMenu* headerPopup;

private:
	int colMap[colNr];
	int colRevMap[colNr];
};

class ConfigItem : public Q3ListViewItem {
	typedef class Q3ListViewItem Parent;
public:
	ConfigItem(Q3ListView *parent, ConfigItem *after, struct menu *m, bool v)
	: Parent(parent, after), menu(m), visible(v), goParent(false)
	{
		init();
	}
	ConfigItem(ConfigItem *parent, ConfigItem *after, struct menu *m, bool v)
	: Parent(parent, after), menu(m), visible(v), goParent(false)
	{
		init();
	}
	ConfigItem(Q3ListView *parent, ConfigItem *after, bool v)
	: Parent(parent, after), menu(0), visible(v), goParent(true)
	{
		init();
	}
	~ConfigItem(void);
	void init(void);
	void okRename(int col);
	void updateMenu(void);
	void testUpdateMenu(bool v);
	ConfigList* listView() const
	{
		return (ConfigList*)Parent::listView();
	}
	ConfigItem* firstChild() const
	{
		return (ConfigItem *)Parent::firstChild();
	}
	ConfigItem* nextSibling() const
	{
		return (ConfigItem *)Parent::nextSibling();
	}
	void setText(colIdx idx, const QString& text)
	{
		Parent::setText(listView()->mapIdx(idx), text);
	}
	QString text(colIdx idx) const
	{
		return Parent::text(listView()->mapIdx(idx));
	}
	void setPixmap(colIdx idx, const QPixmap& pm)
	{
		Parent::setPixmap(listView()->mapIdx(idx), pm);
	}
	const QPixmap* pixmap(colIdx idx) const
	{
		return Parent::pixmap(listView()->mapIdx(idx));
	}
	void paintCell(QPainter* p, const QColorGroup& cg, int column, int width, int align);

	ConfigItem* nextItem;
	struct menu *menu;
	bool visible;
	bool goParent;
};

class ConfigLineEdit : public QLineEdit {
	Q_OBJECT
	typedef class QLineEdit Parent;
public:
	ConfigLineEdit(ConfigView* parent);
	ConfigView* parent(void) const
	{
		return (ConfigView*)Parent::parent();
	}
	void show(ConfigItem *i);
	void keyPressEvent(QKeyEvent *e);

public:
	ConfigItem *item;
};

class ConfigView : public Q3VBox {
	Q_OBJECT
	typedef class Q3VBox Parent;
public:
	ConfigView(QWidget* parent, const char *name = 0);
	~ConfigView(void);
	static void updateList(ConfigItem* item);
	static void updateListAll(void);

	bool showName(void) const { return list->showName; }
	bool showRange(void) const { return list->showRange; }
	bool showData(void) const { return list->showData; }
public slots:
	void setShowName(bool);
	void setShowRange(bool);
	void setShowData(bool);
	void setOptionMode(QAction *);
signals:
	void showNameChanged(bool);
	void showRangeChanged(bool);
	void showDataChanged(bool);
public:
	ConfigList* list;
	ConfigLineEdit* lineEdit;

	static ConfigView* viewList;
	ConfigView* nextView;

	static QAction *showNormalAction;
	static QAction *showAllAction;
	static QAction *showPromptAction;
};

class ConfigInfoView : public Q3TextBrowser {
	Q_OBJECT
	typedef class Q3TextBrowser Parent;
public:
	ConfigInfoView(QWidget* parent, const char *name = 0);
	bool showDebug(void) const { return _showDebug; }

public slots:
	void setInfo(struct menu *menu);
	void saveSettings(void);
	void setShowDebug(bool);

signals:
	void showDebugChanged(bool);
	void menuSelected(struct menu *);

protected:
	void symbolInfo(void);
	void menuInfo(void);
	QString debug_info(struct symbol *sym);
	static QString print_filter(const QString &str);
	static void expr_print_help(void *data, struct symbol *sym, const char *str);
	Q3PopupMenu* createPopupMenu(const QPoint& pos);
	void contentsContextMenuEvent(QContextMenuEvent *e);

	struct symbol *sym;
	struct menu *_menu;
	bool _showDebug;
};

class ConfigSearchWindow : public QDialog {
	Q_OBJECT
	typedef class QDialog Parent;
public:
	ConfigSearchWindow(ConfigMainWindow* parent, const char *name = 0);

public slots:
	void saveSettings(void);
	void search(void);

protected:
	QLineEdit* editField;
	QPushButton* searchButton;
	QSplitter* split;
	ConfigView* list;
	ConfigInfoView* info;

	struct symbol **result;
};

class ConfigMainWindow : public Q3MainWindow {
	Q_OBJECT

	static Q3Action *saveAction;
	static void conf_changed(void);
public:
	ConfigMainWindow(void);
public slots:
	void changeMenu(struct menu *);
	void setMenuLink(struct menu *);
	void listFocusChanged(void);
	void goBack(void);
	void loadConfig(void);
	void saveConfig(void);
	void saveConfigAs(void);
	void searchConfig(void);
	void showSingleView(void);
	void showSplitView(void);
	void showFullView(void);
	void showIntro(void);
	void showAbout(void);
	void saveSettings(void);

protected:
	void closeEvent(QCloseEvent *e);

	ConfigSearchWindow *searchWindow;
	ConfigView *menuView;
	ConfigList *menuList;
	ConfigView *configView;
	ConfigList *configList;
	ConfigInfoView *helpText;
	Q3ToolBar *toolBar;
	Q3Action *backAction;
	QSplitter* split1;
	QSplitter* split2;
};
