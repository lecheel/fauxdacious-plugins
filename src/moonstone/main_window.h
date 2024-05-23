/*
 * main_window.h
 * Copyright 2014 Michał Lipski
 * Copyright 2020 Ariadne Conill
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

#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H

#include <libfauxdcore/hook.h>
#include <libfauxdcore/mainloop.h>
#include <libfauxdcore/playlist.h>
#include <libfauxdqt/libfauxdqt.h>
#include <libfauxdqt/info_bar.h>

#include "../ui-common/dialogs-qt.h"

#include <QMainWindow>
#include <QVBoxLayout>
#include <QMenu>

namespace Moonstone {

class ToolBar;
class PlaylistTabs;

class MainWindow : public QMainWindow {
public:
    MainWindow ();
    ~MainWindow ();

private:
    QString m_config_name;
    QWidget * m_center_widget;
    QVBoxLayout * m_center_layout;
    audqt::InfoBar * m_infobar;
    ToolBar * m_toolbar;

    PlaylistTabs * m_playlist_tabs;

    QMenu * add_menu;
    QAction *m_search_action;
    QAction *m_play_pause_action, *m_stop_action, *m_stop_after_action;
    QAction *m_record_action;
    QAction *m_repeat_action, *m_shuffle_action;

    QueuedFunc m_buffering_timer;
    int m_last_playing;

    void closeEvent (QCloseEvent * e) override;

    void read_settings ();
    void set_title (const QString & title);

    void update_toggles ();
    void update_play_pause ();

    void title_change_cb ();
    void playback_begin_cb ();
    void playback_ready_cb ();
    void pause_cb ();
    void playback_stop_cb ();
    void show_titlebar_toggle_cb ();

    const HookReceiver<MainWindow> //
        hook1{"title change", this, &MainWindow::title_change_cb},
        hook2{"playback begin", this, &MainWindow::playback_begin_cb},
        hook3{"playback ready", this, &MainWindow::title_change_cb},
        hook4{"playback pause", this, &MainWindow::pause_cb},
        hook5{"playback unpause", this, &MainWindow::pause_cb},
        hook6{"playback stop", this, &MainWindow::playback_stop_cb},
        hook7{"set stop_after_current_song", this, &MainWindow::update_toggles},
        hook8{"enable record", this, &MainWindow::update_toggles},
        hook9{"set record", this, &MainWindow::update_toggles},
        hook10{"set repeat", this, &MainWindow::update_toggles},
        hook11{"set shuffle", this, &MainWindow::update_toggles},
        hook12{"toggle minifauxd toolbar", this, &MainWindow::show_titlebar_toggle_cb};
};

}

#endif
