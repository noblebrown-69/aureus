// Single-file C++/GTK+3 journaling application.
// The code is deliberately written using the C API so that a beginner
// can see pointers and manual creation, but wrapped in a C++ class to
// teach object orientation and RAII.  Everything lives in this file.
//
// Features implemented in the JournalApp class:
//  * Main window with a GtkTreeView on the left for entry list
//  * Right‑hand editor: title, tags entries and a wrapped text view
//  * Word/character counter in statusbar
//  * Toolbar with New/Save/Delete/Export buttons, search box, font size
//    selector and dark‑mode toggle
//  * CSS theming (deep blue default, black for dark mode, monospace font)
//  * SQLite3 storage in "aureus.db" next to the executable
//  * Basic prefs table for persisting dark mode and font size
//  * Autosave drafts every five minutes to "autosave.temp"
//  * Export current entry to a sanitized Markdown file
//  * Search‑in‑entry highlighting
//  * Keyboard accelerator Ctrl+S for save
//  * RAII: database closed in destructor, timer removed, etc.

#include <gtk/gtk.h>
#include <sqlite3.h>
#include <string>
#include <sstream>
#include <fstream>
#include <iostream>
#include <vector>
#include <algorithm>
#include <ctime>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <unistd.h>
#include <libgen.h>
#include <limits.h>

struct Chapter {
    int id;
    std::string title;
    std::vector<int> entry_ids;
};

class JournalApp {
private:
    sqlite3* db_;
    GtkWidget* window_;
    GtkWidget* paned_ = nullptr;  // Resizable left/right pane
    GtkWidget* search_entry_; // search within the body of current entry
    GtkWidget* title_entry_;
    GtkWidget* tags_entry_;
    GtkWidget* text_view_;
    GtkTextBuffer* text_buffer_;
    GtkTreeStore* tree_store_;
    GtkTreeView* tree_view_;
    GtkStatusbar* statusbar_;
    GtkToolbar* toolbar_;
    GtkComboBoxText* font_size_combo_;
    GtkSwitch* dark_switch_;
    GtkComboBoxText* theme_combo_;
    GtkWidget* header_bar_;
    GtkCssProvider* css_provider_;

    std::vector<Chapter> chapters_;
    int current_chapter_id_ = 0;
    int dragged_entry_id_ = 0;

    gint64 current_entry_id_ = 0;
    bool unsaved_changes_ = false;
    bool dark_mode_ = false;
    std::string current_font_size_ = "12pt";
    std::string current_theme = "Classic Blue";
    guint autosave_timer_id_ = 0;
    guint status_context_id_ = 0;

    // Window state persistence
    int saved_width_ = 900;
    int saved_height_ = 650;
    int saved_pane_pos_ = 280;

    std::string db_path_;

public:
    JournalApp();
    ~JournalApp();
    void run();

private:
    void init_db();
    void load_preferences();
    void load_chapters();
    void load_window_state();
    void save_window_state();
    void save_preference(const std::string& key, const std::string& value);
    void apply_css();
    void refresh_list(const char* query = nullptr);
    void open_entry(int id);
    void new_entry();
    void save_entry();
    void delete_entry();
    void export_entry();
    void update_statusbar();
    void update_font_size(const std::string& size);
    void autosave();
    void on_theme_changed(GtkComboBoxText* combo, gpointer user_data);
    void highlight_search(const std::string& query);
    std::string sanitize_filename(const std::string& input);
    void create_chapter_dialog();
    void create_chapter(const std::string& title);
    void rename_chapter_dialog(int chapter_id);
    void rename_chapter(int chapter_id, const std::string& new_title);
    void delete_chapter_confirm(int chapter_id);
    void delete_chapter(int chapter_id);
    void show_chapter_menu(int chapter_id, GdkEventButton* event);
    void move_entry_to_chapter(int entry_id, int chapter_id);
    void switch_db(const std::string& new_path);
    void on_new_book();
    void on_open_book();
    void on_save_as();

    // static callbacks
    static void on_search_changed(GtkEditable* editable, gpointer user_data);
    static gboolean on_delete_event(GtkWidget* widget, GdkEvent* event, gpointer user_data);
    static void on_row_activated(GtkTreeView* tree_view, GtkTreePath* path,
                                 GtkTreeViewColumn* column, gpointer user_data);
    static void on_new_clicked(GtkButton* button, gpointer user_data);
    static void on_save_clicked(GtkButton* button, gpointer user_data);
    static void on_delete_clicked(GtkButton* button, gpointer user_data);
    static void on_export_clicked(GtkButton* button, gpointer user_data);
    static void on_new_chapter_clicked(GtkButton* button, gpointer user_data);
    static void on_rename_chapter(GtkMenuItem* item, gpointer user_data);
    static void on_delete_chapter(GtkMenuItem* item, gpointer user_data);
    static void on_buffer_changed(GtkTextBuffer* buffer, gpointer user_data);
    static gboolean on_autosave_timeout(gpointer user_data);
    static void on_font_size_changed(GtkComboBoxText* combo, gpointer user_data);
    static void on_dark_switch_notify(GObject* object, GParamSpec* pspec,
                                      gpointer user_data);
    static void on_theme_changed_static(GtkComboBoxText* combo, gpointer user_data);
    static void on_search_entry_changed(GtkSearchEntry* entry, gpointer user_data);
    static gboolean on_tree_button_press(GtkWidget* widget, GdkEventButton* event, gpointer user_data);
    static void on_drag_begin(GtkWidget* widget, GdkDragContext* context, gpointer user_data);
    static void on_drag_data_get(GtkWidget* widget, GdkDragContext* context, GtkSelectionData* selection_data, guint info, guint time, gpointer user_data);
    static void on_drag_data_received(GtkWidget* widget, GdkDragContext* context, gint x, gint y, GtkSelectionData* selection_data, guint info, guint time, gpointer user_data);
    static void on_new_book_button_static(GtkButton* button, gpointer user_data);
    static void on_open_book_button_static(GtkButton* button, gpointer user_data);
    static void on_save_as_button_static(GtkButton* button, gpointer user_data);
};

// --- implementation -------------------------------------------------------

JournalApp::JournalApp() {
    // determine path of executable so we can store DB next to it
    char exe_path[PATH_MAX];
    char* exe_dir = nullptr;
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len != -1) {
        exe_path[len] = '\0';
        exe_dir = dirname(exe_path);
        db_path_ = std::string(exe_dir) + "/aureus.db";
    } else {
        db_path_ = "aureus.db";
    }

    // read last db from config file
    std::string config_file = std::string(getenv("HOME")) + "/.aureus_last_db";
    std::ifstream ifs(config_file);
    if (ifs.is_open()) {
        std::string line;
        if (std::getline(ifs, line) && !line.empty()) {
            db_path_ = line;
        }
    }

    std::string initial_db_path = db_path_;

    if (sqlite3_open(db_path_.c_str(), &db_) != SQLITE_OK) {
        std::cerr << "Can't open database: " << sqlite3_errmsg(db_) << ", falling back to default\n";
        db_path_ = std::string(exe_dir ? exe_dir : ".") + "/aureus.db";
        if (sqlite3_open(db_path_.c_str(), &db_) != SQLITE_OK) {
            std::cerr << "Can't open default database: " << sqlite3_errmsg(db_) << '\n';
            std::exit(1);
        }
    }

    init_db();
    load_preferences();
    if (db_path_ != initial_db_path) {
        switch_db(db_path_);
    } else {
        load_chapters();
    }

    // build the user interface

    window_ = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window_), "Aureus");
    gtk_window_set_default_size(GTK_WINDOW(window_), 900, 600);
    g_signal_connect(window_, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    g_signal_connect(window_, "delete-event", G_CALLBACK(on_delete_event), this);

    // CSS provider for theming
    css_provider_ = gtk_css_provider_new();
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
                                              GTK_STYLE_PROVIDER(css_provider_),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    // always use custom headerbar
    header_bar_ = gtk_header_bar_new();
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header_bar_), TRUE);
    gtk_header_bar_set_title(GTK_HEADER_BAR(header_bar_), "Aureus");
    gtk_widget_show(header_bar_);
    gtk_window_set_titlebar(GTK_WINDOW(window_), header_bar_);

    apply_css();

    // accelerators
    GtkAccelGroup* accel = gtk_accel_group_new();
    gtk_window_add_accel_group(GTK_WINDOW(window_), accel);

    // toolbar (modern icons, grouped)
    toolbar_ = GTK_TOOLBAR(gtk_toolbar_new());
    gtk_toolbar_set_style(toolbar_, GTK_TOOLBAR_ICONS);

    // New button
    {
        GtkToolItem* t = gtk_tool_button_new(gtk_image_new_from_icon_name("document-new", GTK_ICON_SIZE_LARGE_TOOLBAR), "New");
        gtk_toolbar_insert(toolbar_, t, -1);
        gtk_widget_set_tooltip_text(GTK_WIDGET(t), "Create new entry (Ctrl+N)");
        g_signal_connect(t, "clicked", G_CALLBACK(on_new_clicked), this);
        gtk_widget_add_accelerator(GTK_WIDGET(t), "clicked", accel,
                                   GDK_KEY_n, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
    }
    // Save button
    {
        GtkToolItem* t = gtk_tool_button_new(gtk_image_new_from_icon_name("document-save", GTK_ICON_SIZE_LARGE_TOOLBAR), "Save");
        gtk_toolbar_insert(toolbar_, t, -1);
        gtk_widget_set_tooltip_text(GTK_WIDGET(t), "Save current entry (Ctrl+S)");
        g_signal_connect(t, "clicked", G_CALLBACK(on_save_clicked), this);
        gtk_widget_add_accelerator(GTK_WIDGET(t), "clicked", accel,
                                   GDK_KEY_s, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
    }
    // Delete button
    {
        GtkToolItem* t = gtk_tool_button_new(gtk_image_new_from_icon_name("edit-delete", GTK_ICON_SIZE_LARGE_TOOLBAR), "Delete");
        gtk_toolbar_insert(toolbar_, t, -1);
        gtk_widget_set_tooltip_text(GTK_WIDGET(t), "Delete selected entry");
        g_signal_connect(t, "clicked", G_CALLBACK(on_delete_clicked), this);
    }
    // Export button
    {
        GtkToolItem* t = gtk_tool_button_new(gtk_image_new_from_icon_name("document-save-as", GTK_ICON_SIZE_LARGE_TOOLBAR), "Export");
        gtk_toolbar_insert(toolbar_, t, -1);
        gtk_widget_set_tooltip_text(GTK_WIDGET(t), "Export entry to Markdown");
        g_signal_connect(t, "clicked", G_CALLBACK(on_export_clicked), this);
    }
    // New Book button
    {
        GtkToolItem* t = gtk_tool_button_new(gtk_image_new_from_icon_name("document-new", GTK_ICON_SIZE_LARGE_TOOLBAR), "New Book");
        gtk_toolbar_insert(toolbar_, t, -1);
        gtk_widget_set_tooltip_text(GTK_WIDGET(t), "Create a new book database");
        g_signal_connect(t, "clicked", G_CALLBACK(on_new_book_button_static), this);
    }
    // Open Book button
    {
        GtkToolItem* t = gtk_tool_button_new(gtk_image_new_from_icon_name("document-open", GTK_ICON_SIZE_LARGE_TOOLBAR), "Open Book");
        gtk_toolbar_insert(toolbar_, t, -1);
        gtk_widget_set_tooltip_text(GTK_WIDGET(t), "Open an existing book database");
        g_signal_connect(t, "clicked", G_CALLBACK(on_open_book_button_static), this);
    }
    // Save As button
    {
        GtkToolItem* t = gtk_tool_button_new(gtk_image_new_from_icon_name("document-save-as", GTK_ICON_SIZE_LARGE_TOOLBAR), "Save As");
        gtk_toolbar_insert(toolbar_, t, -1);
        gtk_widget_set_tooltip_text(GTK_WIDGET(t), "Save book as a new database file");
        g_signal_connect(t, "clicked", G_CALLBACK(on_save_as_button_static), this);
    }
    // New Chapter button
    {
        GtkToolItem* t = gtk_tool_button_new(gtk_image_new_from_icon_name("folder-new", GTK_ICON_SIZE_LARGE_TOOLBAR), "New Chapter");
        gtk_toolbar_insert(toolbar_, t, -1);
        gtk_widget_set_tooltip_text(GTK_WIDGET(t), "Create new chapter");
        g_signal_connect(t, "clicked", G_CALLBACK(on_new_chapter_clicked), this);
    }

    // flexible separator before the search & prefs widgets
    GtkToolItem* sep = gtk_separator_tool_item_new();
    gtk_separator_tool_item_set_draw(GTK_SEPARATOR_TOOL_ITEM(sep), FALSE);
    gtk_tool_item_set_expand(sep, TRUE);
    gtk_toolbar_insert(toolbar_, sep, -1);

    // search entry (for highlighting)
    search_entry_ = gtk_search_entry_new();
    gtk_widget_set_tooltip_text(GTK_WIDGET(search_entry_), "Search in entry (highlights matches)");
    GtkToolItem* tsearch = gtk_tool_item_new();
    gtk_container_add(GTK_CONTAINER(tsearch), search_entry_);
    gtk_toolbar_insert(toolbar_, tsearch, -1);
    g_signal_connect(search_entry_, "search-changed",
                     G_CALLBACK(on_search_entry_changed), this);

    // font size combo
    font_size_combo_ = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
    gtk_widget_set_tooltip_text(GTK_WIDGET(font_size_combo_), "Change font size");
    gtk_combo_box_text_append_text(font_size_combo_, "10pt");
    gtk_combo_box_text_append_text(font_size_combo_, "12pt");
    gtk_combo_box_text_append_text(font_size_combo_, "14pt");
    // set active from pref
    if (current_font_size_ == "10pt")
        gtk_combo_box_set_active(GTK_COMBO_BOX(font_size_combo_), 0);
    else if (current_font_size_ == "12pt")
        gtk_combo_box_set_active(GTK_COMBO_BOX(font_size_combo_), 1);
    else if (current_font_size_ == "14pt")
        gtk_combo_box_set_active(GTK_COMBO_BOX(font_size_combo_), 2);
    else
        gtk_combo_box_set_active(GTK_COMBO_BOX(font_size_combo_), 1);
    GtkToolItem* tfont = gtk_tool_item_new();
    gtk_container_add(GTK_CONTAINER(tfont), GTK_WIDGET(font_size_combo_));
    gtk_toolbar_insert(toolbar_, tfont, -1);
    g_signal_connect(font_size_combo_, "changed",
                     G_CALLBACK(on_font_size_changed), this);

    // dark mode toggle
    dark_switch_ = GTK_SWITCH(gtk_switch_new());
    gtk_widget_set_tooltip_text(GTK_WIDGET(dark_switch_), "Toggle dark/light theme");
    gtk_switch_set_active(dark_switch_, dark_mode_);
    GtkToolItem* tswitch = gtk_tool_item_new();
    gtk_container_add(GTK_CONTAINER(tswitch), GTK_WIDGET(dark_switch_));
    gtk_toolbar_insert(toolbar_, tswitch, -1);
    g_signal_connect(dark_switch_, "notify::active",
                     G_CALLBACK(on_dark_switch_notify), this);

    // theme switcher
    theme_combo_ = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
    gtk_widget_set_tooltip_text(GTK_WIDGET(theme_combo_), "Select theme");
    gtk_combo_box_text_append_text(theme_combo_, "Classic Blue");
    gtk_combo_box_text_append_text(theme_combo_, "Leather Zibaldone");
    if (current_theme == "Classic Blue")
        gtk_combo_box_set_active(GTK_COMBO_BOX(theme_combo_), 0);
    else if (current_theme == "Leather Zibaldone")
        gtk_combo_box_set_active(GTK_COMBO_BOX(theme_combo_), 1);
    else
        gtk_combo_box_set_active(GTK_COMBO_BOX(theme_combo_), 0);
    GtkToolItem* ttheme = gtk_tool_item_new();
    gtk_container_add(GTK_CONTAINER(ttheme), GTK_WIDGET(theme_combo_));
    gtk_toolbar_insert(toolbar_, ttheme, -1);
    g_signal_connect(theme_combo_, "changed",
                     G_CALLBACK(on_theme_changed_static), this);

    // layout: toolbar on top, paned split with resizable list on left and editor on right

    // create outer vbox to hold toolbar at top
    GtkWidget* outer_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window_), outer_vbox);

    // toolbar at top
    gtk_box_pack_start(GTK_BOX(outer_vbox), GTK_WIDGET(toolbar_), FALSE, FALSE, 0);

    // horizontal paned split for list and editor
    paned_ = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(outer_vbox), paned_, TRUE, TRUE, 0);

    // left pane: list in scrolled window
    GtkWidget* left_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(left_scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_set_border_width(GTK_CONTAINER(left_scroll), 5);
    gtk_paned_pack1(GTK_PANED(paned_), left_scroll, TRUE, TRUE);

    // Tree store: type (0=chapter,1=entry), id, text, weight
    tree_store_ = gtk_tree_store_new(4, G_TYPE_INT, G_TYPE_INT, G_TYPE_STRING, G_TYPE_INT);
    tree_view_ = GTK_TREE_VIEW(gtk_tree_view_new_with_model(GTK_TREE_MODEL(tree_store_)));
    gtk_container_add(GTK_CONTAINER(left_scroll), GTK_WIDGET(tree_view_));

    // Single column for hierarchy
    GtkCellRenderer* renderer = gtk_cell_renderer_text_new();
    GtkTreeViewColumn* column = gtk_tree_view_column_new_with_attributes("Items",
                                                                         renderer,
                                                                         "text", 2,
                                                                         "weight", 3,
                                                                         NULL);
    gtk_tree_view_append_column(tree_view_, column);
    gtk_tree_view_set_headers_visible(tree_view_, FALSE);

    g_signal_connect(tree_view_, "row-activated",
                     G_CALLBACK(on_row_activated), this);

    // Drag and drop setup
    GtkTargetEntry targets[] = {
        {"text/plain", 0, 0}
    };
    gtk_tree_view_enable_model_drag_source(tree_view_, GDK_BUTTON1_MASK, targets, 1, GDK_ACTION_MOVE);
    gtk_tree_view_enable_model_drag_dest(tree_view_, targets, 1, GDK_ACTION_MOVE);

    g_signal_connect(tree_view_, "drag-begin", G_CALLBACK(on_drag_begin), this);
    g_signal_connect(tree_view_, "drag-data-get", G_CALLBACK(on_drag_data_get), this);
    g_signal_connect(tree_view_, "drag-data-received", G_CALLBACK(on_drag_data_received), this);
    g_signal_connect(tree_view_, "button-press-event", G_CALLBACK(on_tree_button_press), this);

    // right pane: vertical box containing editor elements
    GtkWidget* right_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(right_vbox), 5);
    gtk_paned_pack2(GTK_PANED(paned_), right_vbox, TRUE, TRUE);

    // title input
    GtkWidget* title_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget* title_label = gtk_label_new("Title");
    gtk_misc_set_alignment(GTK_MISC(title_label), 0.0, 0.5);
    gtk_box_pack_start(GTK_BOX(title_box), title_label, FALSE, FALSE, 0);
    title_entry_ = gtk_entry_new();
    gtk_widget_set_hexpand(title_entry_, TRUE);
    gtk_box_pack_start(GTK_BOX(title_box), title_entry_, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(right_vbox), title_box, FALSE, FALSE, 0);

    // tags input
    GtkWidget* tags_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget* tags_label = gtk_label_new("Tags (comma-separated)");
    gtk_misc_set_alignment(GTK_MISC(tags_label), 0.0, 0.5);
    gtk_box_pack_start(GTK_BOX(tags_box), tags_label, FALSE, FALSE, 0);
    tags_entry_ = gtk_entry_new();
    gtk_widget_set_hexpand(tags_entry_, TRUE);
    gtk_box_pack_start(GTK_BOX(tags_box), tags_entry_, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(right_vbox), tags_box, FALSE, FALSE, 0);

    // text view area
    GtkWidget* text_scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(text_scrolled),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_hexpand(text_scrolled, TRUE);
    gtk_widget_set_vexpand(text_scrolled, TRUE);
    gtk_box_pack_start(GTK_BOX(right_vbox), text_scrolled, TRUE, TRUE, 0);
    text_view_ = gtk_text_view_new();
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text_view_), GTK_WRAP_WORD);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(text_view_), 10);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(text_view_), 10);
    text_buffer_ = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view_));
    gtk_container_add(GTK_CONTAINER(text_scrolled), text_view_);
    g_signal_connect(text_buffer_, "changed", G_CALLBACK(on_buffer_changed), this);

    // create highlight tag
    gtk_text_buffer_create_tag(text_buffer_, "highlight", "background", "yellow", NULL);

    // buttons hbox
    GtkWidget* button_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget* save_button = gtk_button_new_with_label("Save");
    gtk_box_pack_start(GTK_BOX(button_hbox), save_button, TRUE, TRUE, 0);
    g_signal_connect(save_button, "clicked", G_CALLBACK(on_save_clicked), this);
    gtk_widget_add_accelerator(save_button, "clicked", accel,
                               GDK_KEY_s, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
    GtkWidget* delete_button = gtk_button_new_with_label("Delete");
    gtk_box_pack_start(GTK_BOX(button_hbox), delete_button, TRUE, TRUE, 0);
    g_signal_connect(delete_button, "clicked", G_CALLBACK(on_delete_clicked), this);
    GtkWidget* new_button = gtk_button_new_with_label("New");
    gtk_box_pack_start(GTK_BOX(button_hbox), new_button, TRUE, TRUE, 0);
    g_signal_connect(new_button, "clicked", G_CALLBACK(on_new_clicked), this);
    gtk_box_pack_start(GTK_BOX(right_vbox), button_hbox, FALSE, FALSE, 0);

    // statusbar at bottom of outer vbox
    statusbar_ = GTK_STATUSBAR(gtk_statusbar_new());
    gtk_box_pack_start(GTK_BOX(outer_vbox), GTK_WIDGET(statusbar_), FALSE, FALSE, 0);

    // finalize assembly
    refresh_list();
    new_entry();

    // Load saved window state (size, paned position)
    load_window_state();

    // check for autosave draft
    std::ifstream as("autosave.temp");
    if (as) {
        std::string txt((std::istreambuf_iterator<char>(as)),
                        std::istreambuf_iterator<char>());
        gtk_text_buffer_set_text(text_buffer_, txt.c_str(), -1);
        current_entry_id_ = 0;
        unsaved_changes_ = true;
        gtk_message_dialog_new(GTK_WINDOW(window_), GTK_DIALOG_MODAL,
                               GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
                               "Recovered unsaved draft from previous run.");
        as.close();
        std::remove("autosave.temp");
        update_statusbar();
    }

    if (chapters_.empty()) {
        create_chapter("Journal");
    }
    current_chapter_id_ = chapters_.front().id;

    refresh_list();

    // start autosave timer (5 minutes)
    autosave_timer_id_ = g_timeout_add_seconds(300, on_autosave_timeout, this);
}

JournalApp::~JournalApp() {
    if (autosave_timer_id_)
        g_source_remove(autosave_timer_id_);
    sqlite3_close(db_);
}

void JournalApp::run() {
    gtk_widget_show_all(window_);
    // place blinking cursor in content area by default
    gtk_widget_grab_focus(text_view_);
    gtk_main();
}

void JournalApp::init_db() {
    char* err = nullptr;
    const char* sql = "CREATE TABLE IF NOT EXISTS entries("
                      "id INTEGER PRIMARY KEY,"
                      "title TEXT,"
                      "content TEXT,"
                      "tags TEXT,"
                      "created TIMESTAMP,"
                      "chapter_id INTEGER DEFAULT 1"
                      ");";
    sqlite3_exec(db_, sql, 0, 0, &err);
    if (err) { std::cerr << "DB init error: " << err << '\n'; sqlite3_free(err); }

    sql = "CREATE TABLE IF NOT EXISTS chapters("
          "id INTEGER PRIMARY KEY,"
          "title TEXT"
          ");";
    sqlite3_exec(db_, sql, 0, 0, &err);
    if (err) { std::cerr << "Chapters init error: " << err << '\n'; sqlite3_free(err); }

    sql = "CREATE TABLE IF NOT EXISTS prefs(key TEXT PRIMARY KEY, value TEXT);";
    sqlite3_exec(db_, sql, 0, 0, &err);
    if (err) { std::cerr << "Prefs init error: " << err << '\n'; sqlite3_free(err); }

    // Add chapter_id column if not exists
    sql = "ALTER TABLE entries ADD COLUMN chapter_id INTEGER DEFAULT 1;";
    sqlite3_exec(db_, sql, 0, 0, &err);
    // Ignore error if column already exists
    if (err) sqlite3_free(err);

    // Update existing entries to have chapter_id = 1
    sql = "UPDATE entries SET chapter_id = 1 WHERE chapter_id IS NULL OR chapter_id = 0;";
    sqlite3_exec(db_, sql, 0, 0, &err);
    if (err) { std::cerr << "Update chapter_id error: " << err << '\n'; sqlite3_free(err); }
}

void JournalApp::load_chapters() {
    chapters_.clear();
    sqlite3_stmt* stmt;
    const char* sql = "SELECT id,title FROM chapters ORDER BY id";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, NULL) != SQLITE_OK)
        return;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int id = sqlite3_column_int(stmt, 0);
        const char* title = (const char*)sqlite3_column_text(stmt, 1);
        Chapter chap{id, title ? title : "Untitled", {}};
        // load entry_ids
        sqlite3_stmt* stmt2;
        const char* sql2 = "SELECT id FROM entries WHERE chapter_id = ? ORDER BY created";
        if (sqlite3_prepare_v2(db_, sql2, -1, &stmt2, NULL) == SQLITE_OK) {
            sqlite3_bind_int(stmt2, 1, id);
            while (sqlite3_step(stmt2) == SQLITE_ROW) {
                int eid = sqlite3_column_int(stmt2, 0);
                chap.entry_ids.push_back(eid);
            }
            sqlite3_finalize(stmt2);
        }
        chapters_.push_back(chap);
    }
    sqlite3_finalize(stmt);
}

void JournalApp::load_preferences() {
    const char* sql = "SELECT key,value FROM prefs";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, NULL) != SQLITE_OK)
        return;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* key = (const char*)sqlite3_column_text(stmt, 0);
        const char* val = (const char*)sqlite3_column_text(stmt, 1);
        if (strcmp(key, "dark_mode") == 0)
            dark_mode_ = (val && std::string(val) == "1");
        else if (strcmp(key, "font_size") == 0)
            current_font_size_ = val ? val : "12pt";
        else if (strcmp(key, "theme") == 0)
            current_theme = val ? val : "Classic Blue";
        else if (strcmp(key, "window_width") == 0 && val)
            saved_width_ = std::atoi(val);
        else if (strcmp(key, "window_height") == 0 && val)
            saved_height_ = std::atoi(val);
        else if (strcmp(key, "pane_position") == 0 && val)
            saved_pane_pos_ = std::atoi(val);
        else if (strcmp(key, "last_db_path") == 0)
            db_path_ = val ? val : db_path_;
    }
    sqlite3_finalize(stmt);
}

void JournalApp::load_window_state() {
    gtk_window_set_default_size(GTK_WINDOW(window_), saved_width_, saved_height_);
    if (paned_)
        gtk_paned_set_position(GTK_PANED(paned_), saved_pane_pos_);
}

void JournalApp::save_window_state() {
    int w, h;
    gtk_window_get_size(GTK_WINDOW(window_), &w, &h);
    if (w > 200 && h > 200) {  // sane minimum dimensions
        save_preference("window_width", std::to_string(w));
        save_preference("window_height", std::to_string(h));
    }
    if (paned_) {
        int pos = gtk_paned_get_position(GTK_PANED(paned_));
        if (pos > 100) {  // sane minimum for pane position
            save_preference("pane_position", std::to_string(pos));
        }
    }
}

void JournalApp::save_preference(const std::string& key, const std::string& value) {
    const char* sql = "REPLACE INTO prefs(key,value) VALUES(?,?)";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, NULL) != SQLITE_OK)
        return;
    sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, value.c_str(), -1, SQLITE_STATIC);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void JournalApp::switch_db(const std::string& new_path) {
    sqlite3* new_db = nullptr;
    if (sqlite3_open(new_path.c_str(), &new_db) != SQLITE_OK) {
        std::cerr << "Can't open new database: " << sqlite3_errmsg(new_db) << '\n';
        if (new_db) sqlite3_close(new_db);
        return; // stay on current db
    }
    if (db_) {
        sqlite3_close(db_);
    }
    db_ = new_db;
    db_path_ = new_path;
    // write to config file
    std::string config_file = std::string(getenv("HOME")) + "/.aureus_last_db";
    std::ofstream ofs(config_file);
    ofs << db_path_ << std::endl;
    init_db();
    load_preferences();
    load_chapters();
    refresh_list();
    new_entry();
    save_preference("last_db_path", db_path_);
}

void JournalApp::apply_css() {
    // build CSS string dynamically based on current_theme, dark_mode_ and current_font_size_
    std::string bg, fg, editor_bg, editor_fg, button_bg, font_family;
    if (current_theme == "Classic Blue") {
        bg = dark_mode_ ? "#000000" : "#003366";
        fg = "#FFFFFF";
        editor_bg = bg;
        editor_fg = fg;
        button_bg = "#001F3F";
        font_family = "monospace";
    } else if (current_theme == "Leather Zibaldone") {
        bg = "#3C2F2F";
        fg = "#D4AF37";
        editor_bg = "#F5E8C7";
        editor_fg = "#000000";
        button_bg = "#2A1F1F";
        font_family = "Georgia, serif";
    } else {
        // fallback
        bg = "#003366";
        fg = "#FFFFFF";
        editor_bg = bg;
        editor_fg = fg;
        button_bg = "#001F3F";
        font_family = "monospace";
    }
    std::string css =
        "window, treeview, statusbar { background-color: " + bg + "; color: " + fg + "; }\n"
        "entry { background-color: " + editor_bg + "; color: " + editor_fg + "; "
            "font-family: " + font_family + "; font-size: " + current_font_size_ + "; "
            "border: 1px solid " + fg + "; padding: 5px; }\n"
        "textview { background-color: " + editor_bg + "; color: " + editor_fg + "; "
            "font-family: " + font_family + "; font-size: " + current_font_size_ + "; }\n"
        "textview text { background-color: " + editor_bg + "; color: " + editor_fg + "; "
            "font-family: " + font_family + "; font-size: " + current_font_size_ + "; }\n"
        "button { background-color: " + button_bg + "; color: " + fg + "; border-radius: 5px; }\n"
        "highlight { background-color: #FFFF00; color: #000000; }\n"
        "toolbar { padding: 5px; }\n";

    if (current_theme == "Leather Zibaldone") {
        css += "headerbar { background-color: #3C2F2F; color: #D4AF37; border-bottom: 2px solid #D4AF37; font-family: \"Georgia, serif\"; }\n"
               "headerbar title { color: #D4AF37; font-weight: bold; }\n"
               "toolbar { background-color: #8B6F47; color: #D4AF37; font-family: \"Georgia, serif\"; }\n";
    } else {
        css += "headerbar { background-color: #F0F0F0; color: #000000; border-bottom: 1px solid #CCCCCC; }\n"
               "headerbar title { color: #000000; font-weight: normal; }\n"
               "toolbar { background-color: transparent; }\n";
    }

    // create a fresh provider each time to avoid accumulating rules
    GtkCssProvider* provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider, css.c_str(), -1, NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
                                              GTK_STYLE_PROVIDER(provider),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

void JournalApp::refresh_list(const char* query) {
    gtk_tree_store_clear(tree_store_);
    for (auto& chap : chapters_) {
        GtkTreeIter chap_iter;
        gtk_tree_store_append(tree_store_, &chap_iter, NULL);
        gtk_tree_store_set(tree_store_, &chap_iter,
                           0, 0, // type chapter
                           1, chap.id,
                           2, chap.title.c_str(),
                           3, PANGO_WEIGHT_BOLD,
                           -1);
        for (int eid : chap.entry_ids) {
            // fetch entry details
            sqlite3_stmt* stmt;
            const char* sql = "SELECT title,created FROM entries WHERE id = ?";
            if (sqlite3_prepare_v2(db_, sql, -1, &stmt, NULL) == SQLITE_OK) {
                sqlite3_bind_int(stmt, 1, eid);
                if (sqlite3_step(stmt) == SQLITE_ROW) {
                    const char* title = (const char*)sqlite3_column_text(stmt, 0);
                    const char* created = (const char*)sqlite3_column_text(stmt, 1);
                    char date_str[20] = "unknown";
                    if (created && strlen(created) >= 16) {
                        strncpy(date_str, created, 16);
                        date_str[16] = '\0';
                    }
                    char entry_text[256];
                    snprintf(entry_text, sizeof(entry_text), "%s - %s", date_str, title ? title : "(untitled)");
                    GtkTreeIter entry_iter;
                    gtk_tree_store_append(tree_store_, &entry_iter, &chap_iter);
                    gtk_tree_store_set(tree_store_, &entry_iter,
                                       0, 1, // type entry
                                       1, eid,
                                       2, entry_text,
                                       3, PANGO_WEIGHT_NORMAL,
                                       -1);
                }
                sqlite3_finalize(stmt);
            }
        }
    }
    gtk_tree_view_expand_all(tree_view_);
}

void JournalApp::open_entry(int id) {
    sqlite3_stmt* stmt;
    const char* sql = "SELECT title,content,tags FROM entries WHERE id = ?";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, NULL) != SQLITE_OK)
        return;
    sqlite3_bind_int(stmt, 1, id);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* title = (const char*)sqlite3_column_text(stmt, 0);
        const char* content = (const char*)sqlite3_column_text(stmt, 1);
        const char* tags = (const char*)sqlite3_column_text(stmt, 2);
        gtk_entry_set_text(GTK_ENTRY(title_entry_), title ? title : "");
        gtk_entry_set_text(GTK_ENTRY(tags_entry_), tags ? tags : "");
        gtk_text_buffer_set_text(text_buffer_, content ? content : "", -1);
        current_entry_id_ = id;
        unsaved_changes_ = false;
        update_statusbar();
        // highlight existing search term
        const char* q = gtk_entry_get_text(GTK_ENTRY(search_entry_));
        highlight_search(q ? q : "");
        // focus content area for convenience
        gtk_widget_grab_focus(text_view_);
    }
    sqlite3_finalize(stmt);
}

void JournalApp::new_entry() {
    time_t now = time(NULL);
    struct tm* t = localtime(&now);
    char buf[20];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", t);
    gtk_entry_set_text(GTK_ENTRY(title_entry_), buf);
    gtk_entry_set_text(GTK_ENTRY(tags_entry_), "");
    gtk_text_buffer_set_text(text_buffer_, "", -1);
    current_entry_id_ = 0;
    unsaved_changes_ = false;
    update_statusbar();
    gtk_widget_grab_focus(text_view_);
}

void JournalApp::save_entry() {
    const char* title_c = gtk_entry_get_text(GTK_ENTRY(title_entry_));
    const char* tags_c = gtk_entry_get_text(GTK_ENTRY(tags_entry_));
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(text_buffer_, &start, &end);
    char* content_c = gtk_text_buffer_get_text(text_buffer_, &start, &end, FALSE);
    std::string title = title_c ? title_c : "";
    std::string tags = tags_c ? tags_c : "";
    std::string content = content_c ? content_c : "";
    g_free(content_c);

    if (title.empty()) {
        time_t now = time(NULL);
        struct tm* t = localtime(&now);
        char buf[20];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", t);
        title = buf;
        gtk_entry_set_text(GTK_ENTRY(title_entry_), title.c_str());
    }

    char timestamp[20];
    time_t now = time(NULL);
    struct tm* t = localtime(&now);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", t);

    sqlite3_stmt* stmt;
    const char* sql;
    if (current_entry_id_ > 0) {
        sql = "UPDATE entries SET title=?, content=?, tags=?, created=? WHERE id=?";
    } else {
        sql = "INSERT INTO entries(title,content,tags,created,chapter_id) VALUES(?,?,?,?,?)";
    }
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, NULL) != SQLITE_OK)
        return;
    sqlite3_bind_text(stmt, 1, title.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, content.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, tags.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, timestamp, -1, SQLITE_STATIC);
    if (current_entry_id_ > 0) {
        sqlite3_bind_int(stmt, 5, (int)current_entry_id_);
    } else {
        sqlite3_bind_int(stmt, 5, current_chapter_id_);
    }
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        std::cerr << "Failed to save entry: " << sqlite3_errmsg(db_) << '\n';
    } else {
        if (current_entry_id_ == 0) {
            current_entry_id_ = sqlite3_last_insert_rowid(db_);
            // add to chapter
            auto it = std::find_if(chapters_.begin(), chapters_.end(), [this](const Chapter& c){ return c.id == current_chapter_id_; });
            if (it != chapters_.end()) {
                it->entry_ids.push_back(current_entry_id_);
            }
        }
        refresh_list();
        unsaved_changes_ = false;
    }
    sqlite3_finalize(stmt);
}

void JournalApp::delete_entry() {
    GtkTreeSelection* sel = gtk_tree_view_get_selection(tree_view_);
    GtkTreeIter iter;
    GtkTreeModel* model;
    if (gtk_tree_selection_get_selected(sel, &model, &iter)) {
        GValue val = G_VALUE_INIT;
        gtk_tree_model_get_value(model, &iter, 0, &val);
        int id = g_value_get_int(&val);
        g_value_unset(&val);
        GtkWidget* dialog = gtk_message_dialog_new(GTK_WINDOW(window_),
                                                   GTK_DIALOG_MODAL,
                                                   GTK_MESSAGE_QUESTION,
                                                   GTK_BUTTONS_YES_NO,
                                                   "Delete selected entry?");
        int resp = gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        if (resp == GTK_RESPONSE_YES) {
            sqlite3_stmt* stmt;
            const char* sql = "DELETE FROM entries WHERE id = ?";
            if (sqlite3_prepare_v2(db_, sql, -1, &stmt, NULL) == SQLITE_OK) {
                sqlite3_bind_int(stmt, 1, id);
                sqlite3_step(stmt);
                sqlite3_finalize(stmt);
            }
            load_chapters();
            new_entry();
            refresh_list();
        }
    } else {
        GtkWidget* dialog = gtk_message_dialog_new(GTK_WINDOW(window_),
                                                   GTK_DIALOG_MODAL,
                                                   GTK_MESSAGE_WARNING,
                                                   GTK_BUTTONS_OK,
                                                   "No entry selected.");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
    }
}

void JournalApp::export_entry() {
    if (current_entry_id_ <= 0)
        return;
    // reuse open_entry logic to fetch latest text
    sqlite3_stmt* stmt;
    const char* sql = "SELECT title,content FROM entries WHERE id = ?";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, NULL) != SQLITE_OK) return;
    sqlite3_bind_int(stmt, 1, (int)current_entry_id_);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* title = (const char*)sqlite3_column_text(stmt, 0);
        const char* content = (const char*)sqlite3_column_text(stmt, 1);
        std::string fname = "Entry-" + sanitize_filename(title ? title : "untitled") + ".md";
        std::ofstream ofs(fname);
        if (ofs) {
            ofs << "# " << (title ? title : "") << "\n\n";
            ofs << (content ? content : "") << "\n";
            ofs.close();
        }
    }
    sqlite3_finalize(stmt);
}

void JournalApp::update_statusbar() {
    gtk_statusbar_pop(statusbar_, status_context_id_);
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(text_buffer_, &start, &end);
    char* txt = gtk_text_buffer_get_text(text_buffer_, &start, &end, FALSE);
    std::string s = txt ? txt : "";
    g_free(txt);
    size_t chars = s.length();
    size_t words = 0;
    std::istringstream iss(s);
    std::string w;
    while (iss >> w) ++words;
    std::ostringstream oss;
    oss << "Words: " << words << " | Chars: " << chars;
    gtk_statusbar_push(statusbar_, status_context_id_, oss.str().c_str());
}

void JournalApp::update_font_size(const std::string& size) {
    current_font_size_ = size;
    apply_css();
    save_preference("font_size", size);
}

void JournalApp::autosave() {
    if (unsaved_changes_ && current_entry_id_ > 0) {
        GtkTextIter start, end;
        gtk_text_buffer_get_bounds(text_buffer_, &start, &end);
        char* txt = gtk_text_buffer_get_text(text_buffer_, &start, &end, FALSE);
        if (txt) {
            std::ofstream ofs("autosave.temp");
            ofs << txt;
            ofs.close();
            g_free(txt);
        }
    }
}

void JournalApp::highlight_search(const std::string& query) {
    GtkTextIter start, match_start, match_end;
    gtk_text_buffer_get_start_iter(text_buffer_, &start);
    gtk_text_buffer_get_end_iter(text_buffer_, &match_end);
    // clear any existing highlights first
    gtk_text_buffer_remove_tag_by_name(text_buffer_, "highlight", &start, &match_end);
    if (query.empty())
        return;
    gtk_text_buffer_get_start_iter(text_buffer_, &start);
    while (gtk_text_iter_forward_search(&start, query.c_str(),
                                        (GtkTextSearchFlags)(GTK_TEXT_SEARCH_TEXT_ONLY | GTK_TEXT_SEARCH_CASE_INSENSITIVE),
                                        &match_start, &match_end, NULL)) {
        gtk_text_buffer_apply_tag_by_name(text_buffer_, "highlight", &match_start, &match_end);
        start = match_end;
    }
}

std::string JournalApp::sanitize_filename(const std::string& input) {
    std::string out;
    for (char c : input) {
        if (isalnum((unsigned char)c) || c == '-' || c == '_')
            out += c;
        else
            out += '_';
    }
    return out;
}

void JournalApp::create_chapter_dialog() {
    GtkWidget* dialog = gtk_dialog_new_with_buttons("New Chapter", GTK_WINDOW(window_), GTK_DIALOG_MODAL,
                                                    "_OK", GTK_RESPONSE_OK,
                                                    "_Cancel", GTK_RESPONSE_CANCEL, NULL);
    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget* entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Chapter title");
    gtk_box_pack_start(GTK_BOX(content), entry, TRUE, TRUE, 5);
    gtk_widget_show_all(dialog);
    int resp = gtk_dialog_run(GTK_DIALOG(dialog));
    if (resp == GTK_RESPONSE_OK) {
        const char* title = gtk_entry_get_text(GTK_ENTRY(entry));
        if (title && strlen(title) > 0) {
            create_chapter(title);
        }
    }
    gtk_widget_destroy(dialog);
}

void JournalApp::create_chapter(const std::string& title) {
    sqlite3_stmt* stmt;
    const char* sql = "INSERT INTO chapters(title) VALUES(?)";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, title.c_str(), -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_DONE) {
            int id = sqlite3_last_insert_rowid(db_);
            Chapter chap{id, title, {}};
            chapters_.push_back(chap);
            refresh_list();
        }
        sqlite3_finalize(stmt);
    }
}

void JournalApp::rename_chapter_dialog(int chapter_id) {
    auto it = std::find_if(chapters_.begin(), chapters_.end(), [chapter_id](const Chapter& c){ return c.id == chapter_id; });
    if (it != chapters_.end()) {
        GtkWidget* dialog = gtk_dialog_new_with_buttons("Rename Chapter", GTK_WINDOW(window_), GTK_DIALOG_MODAL,
                                                        "_OK", GTK_RESPONSE_OK,
                                                        "_Cancel", GTK_RESPONSE_CANCEL, NULL);
        GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
        GtkWidget* entry = gtk_entry_new();
        gtk_entry_set_text(GTK_ENTRY(entry), it->title.c_str());
        gtk_box_pack_start(GTK_BOX(content), entry, TRUE, TRUE, 5);
        gtk_widget_show_all(dialog);
        int resp = gtk_dialog_run(GTK_DIALOG(dialog));
        if (resp == GTK_RESPONSE_OK) {
            const char* new_title = gtk_entry_get_text(GTK_ENTRY(entry));
            if (new_title && strlen(new_title) > 0) {
                rename_chapter(chapter_id, new_title);
            }
        }
        gtk_widget_destroy(dialog);
    }
}

void JournalApp::delete_chapter_confirm(int chapter_id) {
    GtkWidget* dialog = gtk_message_dialog_new(GTK_WINDOW(window_), GTK_DIALOG_MODAL,
                                               GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
                                               "Delete chapter and all its entries?");
    int resp = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    if (resp == GTK_RESPONSE_YES) {
        delete_chapter(chapter_id);
    }
}

void JournalApp::rename_chapter(int chapter_id, const std::string& new_title) {
    sqlite3_stmt* stmt;
    const char* sql = "UPDATE chapters SET title=? WHERE id=?";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, new_title.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 2, chapter_id);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        auto it = std::find_if(chapters_.begin(), chapters_.end(), [chapter_id](const Chapter& c){ return c.id == chapter_id; });
        if (it != chapters_.end()) {
            it->title = new_title;
            refresh_list();
        }
    }
}

void JournalApp::delete_chapter(int chapter_id) {
    // Move entries to default chapter (id=1)
    sqlite3_stmt* stmt;
    const char* sql = "UPDATE entries SET chapter_id=1 WHERE chapter_id=?";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, chapter_id);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
    // Delete chapter
    sql = "DELETE FROM chapters WHERE id=?";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, chapter_id);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
    chapters_.erase(std::remove_if(chapters_.begin(), chapters_.end(), [chapter_id](const Chapter& c){ return c.id == chapter_id; }), chapters_.end());
    refresh_list();
}

void JournalApp::show_chapter_menu(int chapter_id, GdkEventButton* event) {
    GtkWidget* menu = gtk_menu_new();
    GtkWidget* rename_item = gtk_menu_item_new_with_label("Rename Chapter");
    GtkWidget* delete_item = gtk_menu_item_new_with_label("Delete Chapter");
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), rename_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), delete_item);
    gtk_widget_show_all(menu);

    g_object_set_data(G_OBJECT(rename_item), "app", this);
    g_object_set_data(G_OBJECT(rename_item), "chapter_id", GINT_TO_POINTER(chapter_id));
    g_signal_connect(rename_item, "activate", G_CALLBACK(on_rename_chapter), NULL);

    g_object_set_data(G_OBJECT(delete_item), "app", this);
    g_object_set_data(G_OBJECT(delete_item), "chapter_id", GINT_TO_POINTER(chapter_id));
    g_signal_connect(delete_item, "activate", G_CALLBACK(on_delete_chapter), NULL);

    gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent*)event);
}

void JournalApp::move_entry_to_chapter(int entry_id, int chapter_id) {
    sqlite3_stmt* stmt;
    const char* sql = "UPDATE entries SET chapter_id=? WHERE id=?";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, chapter_id);
        sqlite3_bind_int(stmt, 2, entry_id);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        // Update vectors
        for (auto& chap : chapters_) {
            chap.entry_ids.erase(std::remove(chap.entry_ids.begin(), chap.entry_ids.end(), entry_id), chap.entry_ids.end());
        }
        auto it = std::find_if(chapters_.begin(), chapters_.end(), [chapter_id](const Chapter& c){ return c.id == chapter_id; });
        if (it != chapters_.end()) {
            it->entry_ids.push_back(entry_id);
        }
        refresh_list();
    }
}

// Static callbacks
void JournalApp::on_drag_begin(GtkWidget* widget, GdkDragContext* context, gpointer user_data) {
    auto* app = static_cast<JournalApp*>(user_data);
    GtkTreeSelection* selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(widget));
    GtkTreeModel* model;
    GtkTreeIter iter;
    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        GValue value = G_VALUE_INIT;
        gtk_tree_model_get_value(model, &iter, 0, &value);
        int type = g_value_get_int(&value);
        g_value_unset(&value);
        if (type == 1) { // entry
            gtk_tree_model_get_value(model, &iter, 1, &value);
            app->dragged_entry_id_ = g_value_get_int(&value);
            g_value_unset(&value);
        }
    }
}

void JournalApp::on_drag_data_get(GtkWidget* widget, GdkDragContext* context, GtkSelectionData* data, guint info, guint time, gpointer user_data) {
    auto* app = static_cast<JournalApp*>(user_data);
    if (app->dragged_entry_id_ > 0) {
        char buf[32];
        sprintf(buf, "%d", app->dragged_entry_id_);
        gtk_selection_data_set_text(data, buf, -1);
    }
}

void JournalApp::on_drag_data_received(GtkWidget* widget, GdkDragContext* context, gint x, gint y, GtkSelectionData* data, guint info, guint time, gpointer user_data) {
    auto* app = static_cast<JournalApp*>(user_data);
    GtkTreePath* path;
    GtkTreeViewDropPosition pos;
    if (gtk_tree_view_get_dest_row_at_pos(GTK_TREE_VIEW(widget), x, y, &path, &pos)) {
        GtkTreeIter iter;
        if (gtk_tree_model_get_iter(GTK_TREE_MODEL(app->tree_store_), &iter, path)) {
            GValue value = G_VALUE_INIT;
            gtk_tree_model_get_value(GTK_TREE_MODEL(app->tree_store_), &iter, 0, &value);
            int type = g_value_get_int(&value);
            g_value_unset(&value);
            if (type == 0) { // chapter
                gtk_tree_model_get_value(GTK_TREE_MODEL(app->tree_store_), &iter, 1, &value);
                int chapter_id = g_value_get_int(&value);
                g_value_unset(&value);
                const char* text = (const char*)gtk_selection_data_get_text(data);
                if (text) {
                    int entry_id = atoi(text);
                    app->move_entry_to_chapter(entry_id, chapter_id);
                }
            }
        }
        gtk_tree_path_free(path);
    }
    gtk_drag_finish(context, TRUE, FALSE, time);
}

gboolean JournalApp::on_tree_button_press(GtkWidget* widget, GdkEventButton* event, gpointer user_data) {
    auto* app = static_cast<JournalApp*>(user_data);
    if (event->button == 3) { // right click
        GtkTreePath* path;
        if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget), event->x, event->y, &path, NULL, NULL, NULL)) {
            GtkTreeIter iter;
            if (gtk_tree_model_get_iter(GTK_TREE_MODEL(app->tree_store_), &iter, path)) {
                GValue value = G_VALUE_INIT;
                gtk_tree_model_get_value(GTK_TREE_MODEL(app->tree_store_), &iter, 0, &value);
                int type = g_value_get_int(&value);
                g_value_unset(&value);
                if (type == 0) { // chapter
                    gtk_tree_model_get_value(GTK_TREE_MODEL(app->tree_store_), &iter, 1, &value);
                    int chapter_id = g_value_get_int(&value);
                    g_value_unset(&value);
                    app->show_chapter_menu(chapter_id, event);
                }
            }
            gtk_tree_path_free(path);
        }
        return TRUE;
    }
    return FALSE;
}





// static callbacks delegate to instance methods
gboolean JournalApp::on_delete_event(GtkWidget* widget, GdkEvent* event, gpointer user_data) {
    JournalApp* app = static_cast<JournalApp*>(user_data);
    app->save_window_state();
    return FALSE;  // allow window to close
}

void JournalApp::on_search_changed(GtkEditable* editable, gpointer user_data) {
    JournalApp* app = static_cast<JournalApp*>(user_data);
    const char* q = gtk_entry_get_text(GTK_ENTRY(editable));
    app->refresh_list(q);
}

void JournalApp::on_row_activated(GtkTreeView* tree_view, GtkTreePath* path,
                                  GtkTreeViewColumn* column, gpointer user_data) {
    JournalApp* app = static_cast<JournalApp*>(user_data);
    GtkTreeIter iter;
    if (gtk_tree_model_get_iter(GTK_TREE_MODEL(app->tree_store_), &iter, path)) {
        GValue vtype = G_VALUE_INIT;
        gtk_tree_model_get_value(GTK_TREE_MODEL(app->tree_store_), &iter, 0, &vtype);
        int type = g_value_get_int(&vtype);
        g_value_unset(&vtype);
        if (type == 1) { // entry
            GValue vid = G_VALUE_INIT;
            gtk_tree_model_get_value(GTK_TREE_MODEL(app->tree_store_), &iter, 1, &vid);
            int id = g_value_get_int(&vid);
            g_value_unset(&vid);
            // set current chapter to the parent
            GtkTreeIter parent;
            if (gtk_tree_model_iter_parent(GTK_TREE_MODEL(app->tree_store_), &parent, &iter)) {
                GValue vcid = G_VALUE_INIT;
                gtk_tree_model_get_value(GTK_TREE_MODEL(app->tree_store_), &parent, 1, &vcid);
                app->current_chapter_id_ = g_value_get_int(&vcid);
                g_value_unset(&vcid);
            }
            app->open_entry(id);
        } else if (type == 0) { // chapter
            // expand/collapse
            if (gtk_tree_view_row_expanded(tree_view, path)) {
                gtk_tree_view_collapse_row(tree_view, path);
            } else {
                gtk_tree_view_expand_row(tree_view, path, FALSE);
            }
        }
    }
}

void JournalApp::on_new_clicked(GtkButton* button, gpointer user_data) {
    static_cast<JournalApp*>(user_data)->new_entry();
}
void JournalApp::on_save_clicked(GtkButton* button, gpointer user_data) {
    static_cast<JournalApp*>(user_data)->save_entry();
}
void JournalApp::on_delete_clicked(GtkButton* button, gpointer user_data) {
    static_cast<JournalApp*>(user_data)->delete_entry();
}
void JournalApp::on_export_clicked(GtkButton* button, gpointer user_data) {
    static_cast<JournalApp*>(user_data)->export_entry();
}
void JournalApp::on_new_book_button_static(GtkButton* button, gpointer user_data) {
    static_cast<JournalApp*>(user_data)->on_new_book();
}
void JournalApp::on_open_book_button_static(GtkButton* button, gpointer user_data) {
    static_cast<JournalApp*>(user_data)->on_open_book();
}
void JournalApp::on_save_as_button_static(GtkButton* button, gpointer user_data) {
    static_cast<JournalApp*>(user_data)->on_save_as();
}
void JournalApp::on_new_chapter_clicked(GtkButton* button, gpointer user_data) {
    static_cast<JournalApp*>(user_data)->create_chapter_dialog();
}
void JournalApp::on_buffer_changed(GtkTextBuffer* buffer, gpointer user_data) {
    JournalApp* app = static_cast<JournalApp*>(user_data);
    app->unsaved_changes_ = true;
    app->update_statusbar();
    const char* q = gtk_entry_get_text(GTK_ENTRY(app->search_entry_));
    app->highlight_search(q ? q : "");
}

gboolean JournalApp::on_autosave_timeout(gpointer user_data) {
    JournalApp* app = static_cast<JournalApp*>(user_data);
    app->autosave();
    return TRUE; // continue
}

void JournalApp::on_font_size_changed(GtkComboBoxText* combo, gpointer user_data) {
    JournalApp* app = static_cast<JournalApp*>(user_data);
    gchar* size = gtk_combo_box_text_get_active_text(combo);
    if (size) {
        app->update_font_size(size);
        g_free(size);
    }
}

void JournalApp::on_dark_switch_notify(GObject* object, GParamSpec* pspec,
                                       gpointer user_data) {
    JournalApp* app = static_cast<JournalApp*>(user_data);
    gboolean active = gtk_switch_get_active(GTK_SWITCH(object));
    app->dark_mode_ = active;
    app->apply_css();
    app->save_preference("dark_mode", active ? "1" : "0");
}

void JournalApp::on_theme_changed_static(GtkComboBoxText* combo, gpointer user_data) {
    JournalApp* app = static_cast<JournalApp*>(user_data);
    app->on_theme_changed(combo, user_data);
}

void JournalApp::on_theme_changed(GtkComboBoxText* combo, gpointer user_data) {
    gchar* theme = gtk_combo_box_text_get_active_text(combo);
    if (theme) {
        current_theme = theme;
        apply_css();
        save_preference("theme", theme);

        g_free(theme);
    }
}

void JournalApp::on_search_entry_changed(GtkSearchEntry* entry, gpointer user_data) {
    JournalApp* app = static_cast<JournalApp*>(user_data);
    const char* text = gtk_entry_get_text(GTK_ENTRY(entry));
    app->highlight_search(text ? text : "");
}

void JournalApp::on_rename_chapter(GtkMenuItem* item, gpointer user_data) {
    JournalApp* app = static_cast<JournalApp*>(g_object_get_data(G_OBJECT(item), "app"));
    int chapter_id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(item), "chapter_id"));
    app->rename_chapter_dialog(chapter_id);
}

void JournalApp::on_delete_chapter(GtkMenuItem* item, gpointer user_data) {
    JournalApp* app = static_cast<JournalApp*>(g_object_get_data(G_OBJECT(item), "app"));
    int chapter_id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(item), "chapter_id"));
    app->delete_chapter_confirm(chapter_id);
}

void JournalApp::on_new_book() {
    GtkWidget* dialog = gtk_file_chooser_dialog_new("New Book", GTK_WINDOW(window_), GTK_FILE_CHOOSER_ACTION_SAVE, "_Cancel", GTK_RESPONSE_CANCEL, "_Save", GTK_RESPONSE_ACCEPT, NULL);
    GtkFileChooser* chooser = GTK_FILE_CHOOSER(dialog);
    gtk_file_chooser_set_current_name(chooser, "new_book.db");
    GtkFileFilter* filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Database files (*.db)");
    gtk_file_filter_add_pattern(filter, "*.db");
    gtk_file_chooser_add_filter(chooser, filter);
    gtk_file_chooser_set_filter(chooser, filter);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char* filename = gtk_file_chooser_get_filename(chooser);
        if (filename) {
            std::string new_path = filename;
            g_free(filename);
            // Create new db
            sqlite3* new_db;
            if (sqlite3_open(new_path.c_str(), &new_db) == SQLITE_OK) {
                // Seed with default chapters
                const char* sql = "CREATE TABLE IF NOT EXISTS entries(id INTEGER PRIMARY KEY,title TEXT,content TEXT,tags TEXT,created TIMESTAMP,chapter_id INTEGER DEFAULT 1);";
                sqlite3_exec(new_db, sql, 0, 0, 0);
                sql = "CREATE TABLE IF NOT EXISTS chapters(id INTEGER PRIMARY KEY,title TEXT);";
                sqlite3_exec(new_db, sql, 0, 0, 0);
                sql = "CREATE TABLE IF NOT EXISTS prefs(key TEXT PRIMARY KEY, value TEXT);";
                sqlite3_exec(new_db, sql, 0, 0, 0);
                sql = "INSERT INTO chapters(title) VALUES('Journal'),('Articles'),('Random');";
                sqlite3_exec(new_db, sql, 0, 0, 0);
                sqlite3_close(new_db);
                // Switch to it
                switch_db(new_path);
            }
        }
    }
    gtk_widget_destroy(dialog);
}

void JournalApp::on_open_book() {
    GtkWidget* dialog = gtk_file_chooser_dialog_new("Open Book", GTK_WINDOW(window_), GTK_FILE_CHOOSER_ACTION_OPEN, "_Cancel", GTK_RESPONSE_CANCEL, "_Open", GTK_RESPONSE_ACCEPT, NULL);
    GtkFileChooser* chooser = GTK_FILE_CHOOSER(dialog);
    GtkFileFilter* filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Database files (*.db)");
    gtk_file_filter_add_pattern(filter, "*.db");
    gtk_file_chooser_add_filter(chooser, filter);
    gtk_file_chooser_set_filter(chooser, filter);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char* filename = gtk_file_chooser_get_filename(chooser);
        if (filename) {
            std::string new_path = filename;
            g_free(filename);
            switch_db(new_path);
        }
    }
    gtk_widget_destroy(dialog);
}

void JournalApp::on_save_as() {
    // Optional, implement later
}



// entry point
int main(int argc, char* argv[]) {
    gtk_init(&argc, &argv);
    JournalApp app;
    app.run();
    return 0;
}
