// NoirNoxis GTK4 — single-file C++ desktop console
// Build: g++ -std=c++17 noirnoxis-gtk.cpp -o noirnoxis $(pkg-config --cflags --libs gtk4)
#include <gtk/gtk.h>
#include <gio/gio.h>
#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

struct App {
    GtkWidget* window = nullptr;
    GtkWidget* entry = nullptr;
    GtkTextBuffer* output = nullptr;
    std::unordered_map<std::string, std::string> utilities;
};

static void write_to(GtkTextBuffer* b, const std::string& s) {
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(b, &end);
    gtk_text_buffer_insert(b, &end, s.c_str(), -1);
}
static void print(App* a, const std::string& s) {
    write_to(a->output, s + "\n");
}
static std::string trim(std::string s) {
    const auto a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}
static std::string storage_path() {
    return std::string(g_get_user_config_dir()) + "/noirnoxis/utilities.ini";
}
static bool save(App* a) {
    const std::string dir = std::string(g_get_user_config_dir()) + "/noirnoxis";
    if (g_mkdir_with_parents(dir.c_str(), 0700) != 0) return false;
    GKeyFile* kf = g_key_file_new();
    for (const auto& [name, code] : a->utilities)
        g_key_file_set_string(kf, "utilities", name.c_str(), code.c_str());
    gsize len = 0;
    gchar* bytes = g_key_file_to_data(kf, &len, nullptr);
    bool ok = g_file_set_contents(storage_path().c_str(), bytes, (gssize)len, nullptr);
    g_free(bytes);
    g_key_file_unref(kf);
    return ok;
}
static void load(App* a) {
    GKeyFile* kf = g_key_file_new();
    if (g_key_file_load_from_file(kf, storage_path().c_str(), G_KEY_FILE_NONE, nullptr)) {
        gsize count = 0;
        gchar** keys = g_key_file_get_keys(kf, "utilities", &count, nullptr);
        for (gsize i = 0; keys && i < count; ++i) {
            gchar* value = g_key_file_get_string(kf, "utilities", keys[i], nullptr);
            if (value) {
                a->utilities[keys[i]] = value;
                g_free(value);
            }
        }
        g_strfreev(keys);
    }
    g_key_file_unref(kf);
}
static bool reserved(const std::string& c) {
    return c == "help" || c == "about" || c == "clear" || c == "date" ||
           c == "time" || c == "pwd" || c == "ls" || c == "whoami" ||
           c == "system" || c == "add" || c == "list" || c == "remove" || c == "exit";
}
static bool valid_name(const std::string& name) {
    if (name.empty() || name.size() > 32 || !std::isalpha((unsigned char)name[0]))
        return false;
    for (unsigned char c : name)
        if (!std::isalnum(c) && c != '_' && c != '-') return false;
    return !reserved(name);
}

struct Job {
    GtkTextBuffer* buffer;
    std::string name;
};
static void job_finished(GObject* source, GAsyncResult* result, gpointer data) {
    Job* job = static_cast<Job*>(data);
    gchar *out = nullptr, *err = nullptr;
    GError* error = nullptr;
    gboolean ok = g_subprocess_communicate_utf8_finish(
        G_SUBPROCESS(source), result, &out, &err, &error);
    if (out && *out) write_to(job->buffer, out);
    if (err && *err) write_to(job->buffer, std::string("[stderr] ") + err);
    if (!ok) {
        write_to(job->buffer, std::string("Error: ") + (error ? error->message : "unknown") + "\n");
    } else if (g_subprocess_get_if_exited(G_SUBPROCESS(source)) &&
               g_subprocess_get_exit_status(G_SUBPROCESS(source)) != 0) {
        write_to(job->buffer, "Exit code: " +
                 std::to_string(g_subprocess_get_exit_status(G_SUBPROCESS(source))) + "\n");
    }
    write_to(job->buffer, "[finished: " + job->name + "]\n");
    g_clear_error(&error);
    g_free(out);
    g_free(err);
    g_object_unref(job->buffer);
    delete job;
}
static void run_custom(App* a, const std::string& name, const std::string& code) {
    GError* error = nullptr;
    GSubprocess* p = g_subprocess_new(
        GSubprocessFlags(G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE),
        &error, "/bin/sh", "-c", code.c_str(), nullptr);
    if (!p) {
        print(a, std::string("Could not run: ") + (error ? error->message : "unknown"));
        g_clear_error(&error);
        return;
    }
    auto* job = new Job{GTK_TEXT_BUFFER(g_object_ref(a->output)), name};
    g_subprocess_communicate_utf8_async(p, nullptr, nullptr, job_finished, job);
    g_object_unref(p);
}

static void add_response(GtkDialog* dialog, int response, gpointer data) {
    App* a = static_cast<App*>(data);
    if (response == GTK_RESPONSE_ACCEPT) {
        auto* name_entry = GTK_EDITABLE(g_object_get_data(G_OBJECT(dialog), "name-entry"));
        auto* code_view = GTK_TEXT_VIEW(g_object_get_data(G_OBJECT(dialog), "code-view"));
        std::string name = trim(gtk_editable_get_text(name_entry));
        GtkTextBuffer* buf = gtk_text_view_get_buffer(code_view);
        GtkTextIter start, end;
        gtk_text_buffer_get_bounds(buf, &start, &end);
        gchar* raw = gtk_text_buffer_get_text(buf, &start, &end, FALSE);
        std::string code = trim(raw ? raw : "");
        g_free(raw);
        if (!valid_name(name)) {
            print(a, "Invalid keyword. Start with a letter; use letters, digits, _ or -; avoid built-in names.");
        } else if (code.empty()) {
            print(a, "Utility code cannot be empty.");
        } else {
            a->utilities[name] = code;
            print(a, save(a) ? "Saved utility: " + name :
                            "Added for this session, but saving to disk failed: " + name);
        }
    }
    gtk_window_destroy(GTK_WINDOW(dialog));
}
static void show_add(App* a) {
    GtkWidget* dialog = gtk_dialog_new_with_buttons(
        "Add NoirNoxis Utility", GTK_WINDOW(a->window), GTK_DIALOG_MODAL,
        "Cancel", GTK_RESPONSE_CANCEL, "Save", GTK_RESPONSE_ACCEPT, nullptr);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 500, 350);
    GtkWidget* area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_widget_set_margin_start(area, 16);
    gtk_widget_set_margin_end(area, 16);
    gtk_widget_set_margin_top(area, 16);
    gtk_widget_set_margin_bottom(area, 16);
    GtkWidget* name = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(name), "Keyword (for example: hello)");
    gtk_box_append(GTK_BOX(area), name);
    gtk_box_append(GTK_BOX(area), gtk_label_new("Shell code (runs only when you invoke the keyword):"));
    GtkWidget* scroller = gtk_scrolled_window_new();
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scroller), 150);
    gtk_widget_set_vexpand(scroller, TRUE);
    GtkWidget* code = gtk_text_view_new();
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(code), TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), code);
    gtk_box_append(GTK_BOX(area), scroller);
    gtk_box_append(GTK_BOX(area), gtk_label_new("Only run code you trust. Example: echo Hello, NoirNoxis!"));
    g_object_set_data(G_OBJECT(dialog), "name-entry", name);
    g_object_set_data(G_OBJECT(dialog), "code-view", code);
    g_signal_connect(dialog, "response", G_CALLBACK(add_response), a);
    gtk_window_present(GTK_WINDOW(dialog));
}

static void execute(App* a, const std::string& raw) {
    std::string line = trim(raw);
    if (line.empty()) return;
    if (line == "clear") {
        gtk_text_buffer_set_text(a->output, "", -1);
        return;
    }
    print(a, "$ " + line);
    std::istringstream input(line);
    std::string command, argument;
    input >> command;
    std::getline(input, argument);
    argument = trim(argument);
    if (command == "help") {
        print(a, "Built-ins: help  about  clear  date  time  pwd  ls  whoami  system\n"
                 "           add  list  remove KEYWORD  exit\n"
                 "Use 'add' to save your own shell-script utility.");
    } else if (command == "about") {
        print(a, "-- About NoirNoxis --\n\n- License: MIT\n- Idea: NoirNoxis is committed to making the terminal better.\n- Next feature: GTK (now available!)");
    } else if (command == "date" || command == "time") {
        GDateTime* now = g_date_time_new_now_local();
        gchar* value = g_date_time_format(now, command == "date" ? "%Y-%m-%d" : "%H:%M:%S");
        print(a, value ? value : "Date unavailable");
        g_free(value);
        g_date_time_unref(now);
    } else if (command == "pwd") {
        gchar* dir = g_get_current_dir();
        print(a, dir);
        g_free(dir);
    } else if (command == "whoami") {
        print(a, g_get_user_name());
    } else if (command == "ls") {
        GDir* dir = g_dir_open(".", 0, nullptr);
        if (!dir) { print(a, "Cannot read the current directory."); return; }
        std::vector<std::string> names;
        const char* entry;
        while ((entry = g_dir_read_name(dir))) names.emplace_back(entry);
        g_dir_close(dir);
        std::sort(names.begin(), names.end());
        for (const auto& name : names) print(a, name);
        if (names.empty()) print(a, "(empty)");
    } else if (command == "system") {
        gchar* os = g_get_os_info(G_OS_INFO_KEY_PRETTY_NAME);
        print(a, std::string("OS: ") + (os ? os : "Unknown") +
                 "\nUser: " + g_get_user_name() +
                 "\nGTK: " + std::to_string(gtk_get_major_version()) + "." +
                 std::to_string(gtk_get_minor_version()));
        g_free(os);
    } else if (command == "add") {
        show_add(a);
    } else if (command == "list") {
        std::vector<std::string> names;
        for (const auto& [name, _] : a->utilities) names.push_back(name);
        std::sort(names.begin(), names.end());
        if (names.empty()) print(a, "No custom utilities yet. Type add.");
        else for (const auto& name : names) print(a, "  " + name);
    } else if (command == "remove") {
        if (argument.empty()) print(a, "Usage: remove KEYWORD");
        else if (a->utilities.erase(argument))
            print(a, save(a) ? "Removed: " + argument : "Removed from session; could not save.");
        else print(a, "Utility not found: " + argument);
    } else if (command == "exit") {
        gtk_window_close(GTK_WINDOW(a->window));
    } else if (a->utilities.count(command) && argument.empty()) {
        run_custom(a, command, a->utilities.at(command));
    } else {
        print(a, "Unknown command. Type help.");
    }
}
static void on_enter(GtkWidget*, gpointer data) {
    auto* a = static_cast<App*>(data);
    std::string line = gtk_editable_get_text(GTK_EDITABLE(a->entry));
    gtk_editable_set_text(GTK_EDITABLE(a->entry), "");
    execute(a, line);
}
static void on_shortcut(GtkButton* button, gpointer data) {
    auto* a = static_cast<App*>(data);
    const char* cmd = static_cast<const char*>(g_object_get_data(G_OBJECT(button), "command"));
    if (cmd) execute(a, cmd);
}
static void activate(GtkApplication* app, gpointer) {
    auto* a = new App();
    a->window = gtk_application_window_new(app);
    g_object_set_data_full(G_OBJECT(a->window), "noirnoxis-app", a,
                           [](gpointer p) { delete static_cast<App*>(p); });
    gtk_window_set_title(GTK_WINDOW(a->window), "NoirNoxis");
    gtk_window_set_default_size(GTK_WINDOW(a->window), 920, 630);
    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(root, 20);
    gtk_widget_set_margin_end(root, 20);
    gtk_widget_set_margin_top(root, 20);
    gtk_widget_set_margin_bottom(root, 20);
    gtk_window_set_child(GTK_WINDOW(a->window), root);

    GtkWidget* title = gtk_label_new("NOIRNOXIS");
    gtk_widget_add_css_class(title, "title");
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(root), title);
    GtkWidget* desc = gtk_label_new("Making the terminal better  •  GTK4 Edition  •  MIT License");
    gtk_widget_set_halign(desc, GTK_ALIGN_START);
    gtk_widget_add_css_class(desc, "subtitle");
    gtk_box_append(GTK_BOX(root), desc);

    GtkWidget* shortcuts = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    for (const char* cmd : {"help", "about", "system", "add", "list", "clear"}) {
        GtkWidget* button = gtk_button_new_with_label(cmd);
        gtk_widget_add_css_class(button, "quick");
        g_object_set_data(G_OBJECT(button), "command", (gpointer)cmd);
        g_signal_connect(button, "clicked", G_CALLBACK(on_shortcut), a);
        gtk_box_append(GTK_BOX(shortcuts), button);
    }
    gtk_box_append(GTK_BOX(root), shortcuts);

    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, TRUE);
    GtkWidget* view = gtk_text_view_new();
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(view), TRUE);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view), GTK_WRAP_WORD_CHAR);
    gtk_widget_add_css_class(view, "terminal");
    a->output = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), view);
    gtk_box_append(GTK_BOX(root), scroll);

    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget* dollar = gtk_label_new("$");
    gtk_widget_add_css_class(dollar, "prompt");
    gtk_box_append(GTK_BOX(row), dollar);
    a->entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(a->entry), "Type a command (help, add, about...)");
    gtk_widget_set_hexpand(a->entry, TRUE);
    g_signal_connect(a->entry, "activate", G_CALLBACK(on_enter), a);
    gtk_box_append(GTK_BOX(row), a->entry);
    GtkWidget* run = gtk_button_new_with_label("Run");
    g_signal_connect(run, "clicked", G_CALLBACK(on_enter), a);
    gtk_box_append(GTK_BOX(row), run);
    gtk_box_append(GTK_BOX(root), row);

    const char* css = R"CSS(
        window { background: #11141b; }
        .title { color: #e6edf7; font-size: 27px; font-weight: 800; }
        .subtitle { color: #9aa8bb; font-size: 12px; }
        .terminal { background: #090e13; color: #9ef2bd; padding: 14px;
                    border-radius: 12px; font-family: monospace; }
        .terminal text { background: #090e13; color: #9ef2bd; }
        entry { background: #222a35; color: #f0f4f9; border-radius: 8px; }
        button { background: #323d4e; color: #ffffff; border-radius: 8px; }
        button:hover { background: #45586d; }
        .prompt { color: #9ef2bd; font-size: 20px; font-weight: 700; }
    )CSS";
    GtkCssProvider* provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider, css, -1);
    gtk_style_context_add_provider_for_display(
        gtk_widget_get_display(a->window), GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);

    load(a);
    print(a, "NoirNoxis GTK4 initialized.\nType help for commands, or add to make a utility.\n");
    gtk_window_present(GTK_WINDOW(a->window));
    gtk_widget_grab_focus(a->entry);
}
int main(int argc, char** argv) {
    GtkApplication* app = gtk_application_new("io.github.samwise1776.noirnoxis", static_cast<GApplicationFlags>(0));
    g_signal_connect(app, "activate", G_CALLBACK(activate), nullptr);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
