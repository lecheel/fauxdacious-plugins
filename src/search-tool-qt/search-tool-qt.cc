/*
 * search-tool-qt.cc
 * Copyright 2011-2018 John Lindgren and Ren� J.V. Bertin
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the documentation
 *    provided with the distribution.
 *
 * This software is provided "as is" and without any warranty, express or
 * implied. In no event shall the authors be liable for any damages arising from
 * the use of this software.
 */

#include <string.h>
#include <glib.h>

#include <QAbstractListModel>
#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QBoxLayout>
#include <QContextMenuEvent>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QPushButton>
#include <QStyledItemDelegate>
#include <QTreeView>
#include <QUrl>

#include <libfauxdcore/audstrings.h>
#include <libfauxdcore/hook.h>
#include <libfauxdcore/i18n.h>
#include <libfauxdcore/playlist.h>
#include <libfauxdcore/plugin.h>
#include <libfauxdcore/preferences.h>
#include <libfauxdcore/mainloop.h>
#include <libfauxdcore/multihash.h>
#include <libfauxdcore/runtime.h>
#include <libfauxdqt/libfauxdqt.h>
#include <libfauxdqt/menu.h>

#define CFG_ID "search-tool"
#define SEARCH_DELAY 300

class SearchToolQt : public GeneralPlugin
{
public:
    static const char * const defaults[];
    static const PreferencesWidget widgets[];
    static const PluginPreferences prefs;

    static constexpr PluginInfo info = {
        N_("Search Tool"),
        PACKAGE,
        nullptr, // about
        & prefs,
        PluginQtOnly
    };

    constexpr SearchToolQt () : GeneralPlugin (info, false) {}

    bool init ();
    void * get_qt_widget ();
    int take_message (const char * code, const void *, int);
};

EXPORT SearchToolQt aud_plugin_instance;

static void trigger_search ();

const char * const SearchToolQt::defaults[] = {
    "max_results", "20",
    "rescan_on_startup", "FALSE",
    nullptr
};

const PreferencesWidget SearchToolQt::widgets[] = {
    WidgetSpin (N_("Number of results to show:"),
        WidgetInt (CFG_ID, "max_results", trigger_search),
         {10, 10000, 10}),
    WidgetCheck (N_("Rescan library at startup"),
        WidgetBool (CFG_ID, "rescan_on_startup"))
};

const PluginPreferences SearchToolQt::prefs = {{widgets}};

enum class SearchField {
    Genre,
    Artist,
    Album,
    Title,
    count
};

struct Key
{
    SearchField field;
    String name;

    bool operator== (const Key & b) const
        { return field == b.field && name == b.name; }
    unsigned hash () const
        { return (unsigned) field + name.hash (); }
};

struct Item
{
    SearchField field;
    String name, folded;
    Item * parent;
    SimpleHash<Key, Item> children;
    Index<int> matches;

    Item (SearchField field, const String & name, Item * parent) :
        field (field),
        name (name),
        folded (str_tolower_utf8 (name)),
        parent (parent) {}

    Item (Item &&) = default;
    Item & operator= (Item &&) = default;
};

class ResultsModel : public QAbstractListModel
{
public:
    void update ();

protected:
    int rowCount (const QModelIndex & parent) const { return m_rows; }
    int columnCount (const QModelIndex & parent) const { return 1; }
    QVariant data (const QModelIndex & index, int role) const;

    Qt::ItemFlags flags (const QModelIndex & index) const
    {
        if (index.isValid ())
            return Qt::ItemIsSelectable | Qt::ItemIsDragEnabled | Qt::ItemIsEnabled;
        else
            return Qt::ItemIsSelectable | Qt::ItemIsEnabled;
    }

    QStringList mimeTypes () const
    {
        return QStringList ("text/uri-list");
    }

    QMimeData * mimeData (const QModelIndexList & indexes) const;

private:
    int m_rows = 0;
};

// Allow rich text in QTreeView entries
// https://stackoverflow.com/questions/1956542/how-to-make-item-view-render-rich-html-text-in-qt
class HtmlDelegate : public QStyledItemDelegate
{
protected:
    void paint (QPainter * painter, const QStyleOptionViewItem & option, const QModelIndex & index) const;
    QSize sizeHint (const QStyleOptionViewItem & option, const QModelIndex & index) const;
};

static void init_text_document (QTextDocument & doc, const QStyleOptionViewItem & option)
{
    doc.setHtml (option.text);
    doc.setDocumentMargin (audqt::sizes.TwoPt);
    doc.setDefaultFont (option.font);
}

void HtmlDelegate::paint (QPainter * painter, const QStyleOptionViewItem & option_, const QModelIndex & index) const
{
    QStyleOptionViewItem option = option_;
    initStyleOption (& option, index);

    QStyle * style = option.widget ? option.widget->style () : qApp->style ();

    QTextDocument doc;
    init_text_document (doc, option);

    /// Painting item without text
    option.text = QString ();
    style->drawControl (QStyle::CE_ItemViewItem, & option, painter);

    QAbstractTextDocumentLayout::PaintContext ctx;

    // Color group logic imitating qcommonstyle.cpp
    QPalette::ColorGroup cg =
        ! (option.state & QStyle::State_Enabled) ? QPalette::Disabled :
        ! (option.state & QStyle::State_Active) ? QPalette::Inactive : QPalette::Normal;

    // Highlighting text if item is selected
    if (option.state & QStyle::State_Selected)
        ctx.palette.setColor (QPalette::Text, option.palette.color (cg, QPalette::HighlightedText));
    else
        ctx.palette.setColor (QPalette::Text, option.palette.color (cg, QPalette::Text));

    QRect textRect = style->subElementRect (QStyle::SE_ItemViewItemText, & option);
    painter->save ();
    painter->translate (textRect.topLeft ());
    painter->setClipRect (textRect.translated (-textRect.topLeft ()));
    doc.documentLayout ()->draw (painter, ctx);
    painter->restore ();
}

QSize HtmlDelegate::sizeHint (const QStyleOptionViewItem & option_, const QModelIndex & index) const
{
    QStyleOptionViewItem option = option_;
    initStyleOption (& option, index);

    QTextDocument doc;
    init_text_document (doc, option);

    return QSize (audqt::sizes.OneInch, doc.size ().height ());
}

class ResultsView : public QTreeView
{
public:
    ResultsView ()
        { setItemDelegate (& m_delegate); }

protected:
    void contextMenuEvent (QContextMenuEvent * event);

private:
    HtmlDelegate m_delegate;
};

static QString create_item_label (int row);

static int playlist_id;
static Index<String> search_terms;

/* Note: added_table is accessed by multiple threads.
 * When adding = true, it may only be accessed by the playlist add thread.
 * When adding = false, it may only be accessed by the UI thread.
 * adding may only be set by the UI thread while holding adding_lock. */
static TinyLock adding_lock;
static bool adding = false;
static SimpleHash<String, bool> added_table;

static SimpleHash<Key, Item> database;
static bool database_valid;
static Index<const Item *> items;
static int hidden_items;

static QueuedFunc search_timer;
static bool search_pending;

static ResultsModel model;
static QLabel * help_label, * wait_label, * stats_label;
static QLineEdit * search_entry;
static QTreeView * results_list;
static QMenu * menu;

void ResultsModel::update ()
{
    int rows = items.len ();
    int keep = aud::min (rows, m_rows);

    if (rows < m_rows)
    {
        beginRemoveRows (QModelIndex (), rows, m_rows - 1);
        m_rows = rows;
        endRemoveRows ();
    }
    else if (rows > m_rows)
    {
        beginInsertRows (QModelIndex (), m_rows, rows - 1);
        m_rows = rows;
        endInsertRows ();
    }

    if (keep > 0)
    {
        auto topLeft = createIndex (0, 0);
        auto bottomRight = createIndex (keep - 1, 0);
        emit dataChanged (topLeft, bottomRight);
    }
}

QVariant ResultsModel::data (const QModelIndex & index, int role) const
{
    if (role == Qt::DisplayRole)
        return create_item_label (index.row ());
    else
        return QVariant ();
}

static void find_playlist ()
{
    playlist_id = -1;

    for (int p = 0; playlist_id < 0 && p < aud_playlist_count (); p ++)
    {
        String title = aud_playlist_get_title (p);
        if (! strcmp (title, _("Library")))
            playlist_id = aud_playlist_get_unique_id (p);
    }
}

static int create_playlist ()
{
    int list = aud_playlist_get_blank ();
    aud_playlist_set_title (list, _("Library"));
    aud_playlist_set_active (list);
    playlist_id = aud_playlist_get_unique_id (list);
    return list;
}

static int get_playlist (bool require_added, bool require_scanned)
{
    if (playlist_id < 0)
        return -1;

    int list = aud_playlist_by_unique_id (playlist_id);

    if (list < 0)
    {
        playlist_id = -1;
        return -1;
    }

    if (require_added && aud_playlist_add_in_progress (list))
        return -1;
    if (require_scanned && aud_playlist_scan_in_progress (list))
        return -1;

    return list;
}

static String get_uri ()
{
    auto to_uri = [] (const char * path)
        { return String (filename_to_uri (path)); };

    String path1 = aud_get_str ("search-tool", "path");
    if (path1[0])
        return strstr (path1, "://") ? path1 : to_uri (path1);

    StringBuf path2 = filename_build ({g_get_home_dir (), "Music"});
    if (g_file_test (path2, G_FILE_TEST_EXISTS))
        return to_uri (path2);

    return to_uri (g_get_home_dir ());
}

static void destroy_database ()
{
    items.clear ();
    hidden_items = 0;
    database.clear ();
    database_valid = false;
}

static void create_database (int list)
{
    destroy_database ();

    int entries = aud_playlist_entry_count (list);

    for (int e = 0; e < entries; e ++)
    {
        Tuple tuple = aud_playlist_entry_get_tuple (list, e, Playlist::NoWait);

        aud::array<SearchField, String> fields;
        fields[SearchField::Genre] = tuple.get_str (Tuple::Genre);
        fields[SearchField::Artist] = tuple.get_str (Tuple::Artist);
        fields[SearchField::Album] = tuple.get_str (Tuple::Album);
        fields[SearchField::Title] = tuple.get_str (Tuple::Title);

        Item * parent = nullptr;
        SimpleHash<Key, Item> * hash = & database;

        for (auto f : aud::range<SearchField> ())
        {
            if (fields[f])
            {
                Key key = {f, fields[f]};
                Item * item = hash->lookup (key);

                if (! item)
                    item = hash->add (key, Item (f, fields[f], parent));

                item->matches.append (e);

                /* genre is outside the normal hierarchy */
                if (f != SearchField::Genre)
                {
                    parent = item;
                    hash = & item->children;
                }
            }
        }
    }

    database_valid = true;
}

static void search_recurse (SimpleHash<Key, Item> & domain, int mask, Index<const Item *> & results)
{
    domain.iterate ([mask, & results] (const Key & key, Item & item)
    {
        int count = search_terms.len ();
        int new_mask = mask;

        for (int t = 0, bit = 1; t < count; t ++, bit <<= 1)
        {
            if (! (new_mask & bit))
                continue; /* skip term if it is already found */

            if (strstr (item.folded, search_terms[t]))
                new_mask &= ~bit; /* we found it */
            else if (! item.children.n_items ())
                break; /* quit early if there are no children to search */
        }

        /* adding an item with exactly one child is redundant, so avoid it */
        if (! new_mask && item.children.n_items () != 1)
            results.append (& item);

        search_recurse (item.children, new_mask, results);
    });
}

static int item_compare (const Item * const & a, const Item * const & b)
{
    if (a->field < b->field)
        return -1;
    if (a->field > b->field)
        return 1;

    int val = str_compare (a->name, b->name);
    if (val)
        return val;

    if (a->parent)
        return b->parent ? item_compare (a->parent, b->parent) : 1;
    else
        return b->parent ? -1 : 0;
}

static int item_compare_pass1 (const Item * const & a, const Item * const & b)
{
    if (a->matches.len () > b->matches.len ())
        return -1;
    if (a->matches.len () < b->matches.len ())
        return 1;

    return item_compare (a, b);
}

static void do_search ()
{
    items.clear ();
    hidden_items = 0;

    if (! database_valid)
        return;

    /* effectively limits number of search terms to 32 */
    search_recurse (database, (1 << search_terms.len ()) - 1, items);

    /* first sort by number of songs per item */
    items.sort (item_compare_pass1);

    int max_results = aud_get_int (CFG_ID, "max_results");
    /* limit to items with most songs */
    if (items.len () > max_results)
    {
        hidden_items = items.len () - max_results;
        items.remove (max_results, -1);
    }

    /* sort by item type, then item name */
    items.sort (item_compare);
}

static bool filter_cb (const char * filename, void * unused)
{
    bool add = false;
    tiny_lock (& adding_lock);

    if (adding)
    {
        bool * added = added_table.lookup (String (filename));

        if ((add = ! added))
            added_table.add (String (filename), true);
        else
            (* added) = true;
    }

    tiny_unlock (& adding_lock);
    return add;
}

static void begin_add (const char * uri)
{
    if (adding)
        return;

    int list = get_playlist (false, false);

    if (list < 0)
        list = create_playlist ();

    /* if possible, store local path for compatibility with older versions */
    StringBuf path = uri_to_filename (uri);
    aud_set_str ("search-tool", "path", path ? path : uri);

    added_table.clear ();

    int entries = aud_playlist_entry_count (list);

    for (int entry = 0; entry < entries; entry ++)
    {
        String filename = aud_playlist_entry_get_filename (list, entry);

        if (! added_table.lookup (filename))
        {
            aud_playlist_entry_set_selected (list, entry, false);
            added_table.add (filename, false);
        }
        else
            aud_playlist_entry_set_selected (list, entry, true);
    }

    aud_playlist_delete_selected (list);

    tiny_lock (& adding_lock);
    adding = true;
    tiny_unlock (& adding_lock);

    Index<PlaylistAddItem> add;
    add.append (String (uri));
    aud_playlist_entry_insert_filtered (list, -1, std::move (add), filter_cb, nullptr, false);
}

static void show_hide_widgets ()
{
    if (playlist_id < 0)
    {
        wait_label->hide ();
        results_list->hide ();
        stats_label->hide ();
        help_label->show ();
    }
    else
    {
        help_label->hide ();

        if (database_valid)
        {
            wait_label->hide ();
            results_list->show ();
            stats_label->show ();
        }
        else
        {
            results_list->hide ();
            stats_label->hide ();
            wait_label->show ();
        }
    }
}

static void search_timeout (void * = nullptr)
{
    do_search ();

    model.update ();

    if (items.len ())
    {
        auto sel = results_list->selectionModel ();
        sel->select (model.index (0, 0), sel->Clear | sel->SelectCurrent);
    }

    int total = items.len () + hidden_items;

    if (hidden_items)
        stats_label->setText ((const char *)
         str_printf (dngettext (PACKAGE, "%d of %d result shown",
         "%d of %d results shown", total), items.len (), total));
    else
        stats_label->setText ((const char *)
         str_printf (dngettext (PACKAGE, "%d result", "%d results", total), total));

    search_timer.stop ();
    search_pending = false;
}

static void trigger_search ()
{
    search_timer.queue (SEARCH_DELAY, search_timeout, nullptr);
    search_pending = true;
}

static void update_database ()
{
    int list = get_playlist (true, true);

    if (list >= 0)
    {
        create_database (list);
        search_timeout ();
    }
    else
    {
        destroy_database ();
        model.update ();
        stats_label->clear ();
    }

    show_hide_widgets ();
}

static void add_complete_cb (void * unused, void * unused2)
{
    int list = get_playlist (true, false);
    if (list < 0)
        return;

    if (adding)
    {
        tiny_lock (& adding_lock);
        adding = false;
        tiny_unlock (& adding_lock);

        int entries = aud_playlist_entry_count (list);

        for (int entry = 0; entry < entries; entry ++)
        {
            String filename = aud_playlist_entry_get_filename (list, entry);
            bool * added = added_table.lookup (filename);

            aud_playlist_entry_set_selected (list, entry, ! added || ! (* added));
        }

        added_table.clear ();

        /* don't clear the playlist if nothing was added */
        if (aud_playlist_selected_count (list) < aud_playlist_entry_count (list))
            aud_playlist_delete_selected (list);
        else
            aud_playlist_select_all (list, false);

        aud_playlist_sort_by_scheme (list, Playlist::Path);
    }

    if (! database_valid && ! aud_playlist_update_pending (list))
        update_database ();
}

static void scan_complete_cb (void * unused, void * unused2)
{
    int list = get_playlist (true, true);
    if (list < 0)
        return;

    if (! database_valid && ! aud_playlist_update_pending (list))
        update_database ();
}

static void playlist_update_cb (void * data, void * unused)
{
    if (! database_valid)
        update_database ();
    else
    {
        int list = get_playlist (true, true);
        if (list < 0 || aud_playlist_update_detail (list).level >= Playlist::Metadata)
            update_database ();
    }
}

static void search_init ()
{
    find_playlist ();

    if (aud_get_bool (CFG_ID, "rescan_on_startup"))
        begin_add (get_uri ());

    update_database ();

    hook_associate ("playlist add complete", add_complete_cb, nullptr);
    hook_associate ("playlist scan complete", scan_complete_cb, nullptr);
    hook_associate ("playlist update", playlist_update_cb, nullptr);
}

static void search_cleanup ()
{
    hook_dissociate ("playlist add complete", add_complete_cb);
    hook_dissociate ("playlist scan complete", scan_complete_cb);
    hook_dissociate ("playlist update", playlist_update_cb);

    search_timer.stop ();
    search_pending = false;

    search_terms.clear ();
    items.clear ();

    tiny_lock (& adding_lock);
    adding = false;
    tiny_unlock (& adding_lock);

    added_table.clear ();
    destroy_database ();

    help_label = wait_label = stats_label = nullptr;
    search_entry = nullptr;
    results_list = nullptr;

    delete menu;
    menu = nullptr;
}

static void do_add (bool play, bool set_title)
{
    if (search_pending)
        search_timeout ();

    int list = aud_playlist_by_unique_id (playlist_id);
    int n_items = items.len ();
    int n_selected = 0;

    Index<PlaylistAddItem> add;
    String title;

    for (auto & idx : results_list->selectionModel ()->selectedRows ())
    {
        int i = idx.row ();
        if (i < 0 || i >= n_items)
            continue;

        const Item * item = items[i];

        for (int entry : item->matches)
        {
            add.append (
                aud_playlist_entry_get_filename (list, entry),
                aud_playlist_entry_get_tuple (list, entry, Playlist::NoWait),
                aud_playlist_entry_get_decoder (list, entry, Playlist::NoWait)
            );
        }

        n_selected ++;
        if (n_selected == 1)
            title = item->name;
    }

    int list2 = aud_playlist_get_active ();
    aud_playlist_entry_insert_batch (list2, -1, std::move (add), play);

    if (set_title && n_selected == 1)
        aud_playlist_set_title (list2, title);
}

static void action_play ()
{
    aud_playlist_set_active (aud_playlist_get_temporary ());
    do_add (true, false);
}

static void action_create_playlist ()
{
    aud_playlist_new ();
    do_add (false, true);
}

static void action_add_to_playlist ()
{
    if (aud_playlist_by_unique_id (playlist_id) != aud_playlist_get_active ())
        do_add (false, false);
}

static QString create_item_label (int row)
{
    static constexpr aud::array<SearchField, const char *> start_tags =
        {"", "<b>", "<i>", ""};
    static constexpr aud::array<SearchField, const char *> end_tags =
        {"", "</b>", "</i>", ""};

    if (row < 0 || row >= items.len ())
        return QString ();

    const Item * item = items[row];

    QString string = start_tags[item->field];

    string += QString ((item->field == SearchField::Genre) ?
                       str_toupper_utf8 (item->name) : item->name).toHtmlEscaped ();

    string += end_tags[item->field];

#ifdef Q_OS_MAC  // Mac-specific font tweaks
    string += "<br>&nbsp;";
#else
    string += "<br><small>&nbsp;";
#endif

    if (item->field != SearchField::Title)
    {
        string += str_printf (dngettext (PACKAGE, "%d song", "%d songs",
         item->matches.len ()), item->matches.len ());

        if (item->field == SearchField::Genre || item->parent)
            string += ' ';
    }

    if (item->field == SearchField::Genre)
    {
        string += _("of this genre");
    }
    else if (item->parent)
    {
        auto parent = (item->parent->parent ? item->parent->parent : item->parent);

        string += (parent->field == SearchField::Album) ? _("on") : _("by");
        string += ' ';
        string += start_tags[parent->field];
        string += QString (parent->name).toHtmlEscaped ();
        string += end_tags[parent->field];
    }

#ifndef Q_OS_MAC  // Mac-specific font tweaks
    string += "</small>";
#endif

    return string;
}

void ResultsView::contextMenuEvent (QContextMenuEvent * event)
{
    static const audqt::MenuItem items[] = {
        audqt::MenuCommand ({N_("_Play"), "media-playback-start"}, action_play),
        audqt::MenuCommand ({N_("_Create Playlist"), "document-new"}, action_create_playlist),
        audqt::MenuCommand ({N_("_Add to Playlist"), "list-add"}, action_add_to_playlist)
    };

    if (! menu)
        menu = audqt::menu_build ({items});

    menu->popup (event->globalPos ());
}

QMimeData * ResultsModel::mimeData (const QModelIndexList & indexes) const
{
    if (search_pending)
        search_timeout ();

    int list = aud_playlist_by_unique_id (playlist_id);

    aud_playlist_select_all (list, false);

    QList<QUrl> urls;
    for (auto & index : indexes)
    {
        int row = index.row ();
        if (row < 0 || row >= items.len ())
            continue;

        for (int entry : items[row]->matches)
        {
            urls.append (QString (aud_playlist_entry_get_filename (list, entry)));
            aud_playlist_entry_set_selected (list, entry, true);
        }
    }

    aud_playlist_cache_selected (list);

    auto data = new QMimeData;
    data->setUrls (urls);
    return data;
}

bool SearchToolQt::init ()
{
    aud_config_set_defaults (CFG_ID, defaults);
    return true;
}

void * SearchToolQt::get_qt_widget ()
{
    search_entry = new QLineEdit;
    search_entry->setContentsMargins (audqt::margins.TwoPt);
    search_entry->setClearButtonEnabled (true);
    search_entry->setPlaceholderText (_("Search library"));

    help_label = new QLabel (_("To import your music library into Fauxdacious, "
     "choose a folder and then click the \"refresh\" icon."));
    help_label->setAlignment (Qt::AlignCenter);
    help_label->setContentsMargins (audqt::margins.EightPt);
    help_label->setWordWrap (true);

    wait_label = new QLabel (_("Please wait ..."));
    wait_label->setAlignment (Qt::AlignCenter);
    wait_label->setContentsMargins (audqt::margins.EightPt);

    results_list = new ResultsView;
    results_list->setFrameStyle (QFrame::NoFrame);
    results_list->setHeaderHidden (true);
    results_list->setIndentation (0);
    results_list->setModel (& model);
    results_list->setSelectionMode (QTreeView::ExtendedSelection);
    results_list->setDragDropMode (QTreeView::DragOnly);

    stats_label = new QLabel;
    stats_label->setAlignment (Qt::AlignCenter);
    stats_label->setContentsMargins (audqt::margins.TwoPt);

#ifdef Q_OS_MAC  // Mac-specific font tweaks
    search_entry->setFont (QApplication::font ("QTreeView"));
    stats_label->setFont (QApplication::font ("QSmallFont"));
#endif

    auto chooser = audqt::file_entry_new (nullptr, _("Choose Folder"),
     QFileDialog::Directory, QFileDialog::AcceptOpen);

    auto button = new QPushButton (audqt::get_icon ("view-refresh"), QString ());
    button->setFlat (true);
    button->setFocusPolicy (Qt::NoFocus);

    auto hbox = audqt::make_hbox (nullptr);
    hbox->setContentsMargins (audqt::margins.TwoPt);

    hbox->addWidget (chooser);
    hbox->addWidget (button);

    auto widget = new QWidget;
    auto vbox = audqt::make_vbox (widget, 0);

    vbox->addWidget (search_entry);
    vbox->addWidget (help_label);
    vbox->addWidget (wait_label);
    vbox->addWidget (results_list);
    vbox->addWidget (stats_label);
    vbox->addLayout (hbox);

    audqt::file_entry_set_uri (chooser, get_uri ());

    search_init ();

    QObject::connect (widget, & QObject::destroyed, search_cleanup);
    QObject::connect (search_entry, & QLineEdit::returnPressed, action_play);
    QObject::connect (results_list, & QTreeView::doubleClicked, action_play);

    QObject::connect (search_entry, & QLineEdit::textEdited, [] (const QString & text)
    {
        search_terms = str_list_to_index (str_tolower_utf8 (text.toUtf8 ()), " ");
        trigger_search ();
    });

    QObject::connect (chooser, & QLineEdit::textChanged, [button] (const QString & text)
        { button->setDisabled (text.isEmpty ()); });

    auto refresh = [chooser] () {
        String uri = audqt::file_entry_get_uri (chooser);
        if (uri)
        {
            audqt::file_entry_set_uri (chooser, uri);  // normalize path
            begin_add (uri);
            update_database ();
        }
    };

    QObject::connect (chooser, & QLineEdit::returnPressed, refresh);
    QObject::connect (button, & QPushButton::clicked, refresh);

    return widget;
}

int SearchToolQt::take_message (const char * code, const void *, int)
{
    if (! strcmp (code, "grab focus") && search_entry)
    {
        search_entry->setFocus (Qt::OtherFocusReason);
        return 0;
    }

    return -1;
}
