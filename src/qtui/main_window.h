/*
 * main_window.h
 * Copyright 2014 Michał Lipski
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

#include <libaudcore/hook.h>
#include <libaudcore/index.h>
#include <libaudcore/mainloop.h>

#include "dialog_windows.h"

#include <QMainWindow>

class InfoBar;
class PlaylistTabs;
class PluginHandle;
class PluginWidget;
class QVBoxLayout;
class StatusBar;

class MainWindow : public QMainWindow
{
public:
    MainWindow ();
    ~MainWindow ();

private:
    QString m_config_name;
    DialogWindows m_dialogs;
    PlaylistTabs * playlistTabs;
    QMenuBar * menuBar;
    InfoBar * infoBar;
    QWidget * centralWidget;
    QVBoxLayout * centralLayout;
    StatusBar * statusBar;

    QAction * toolButtonPlayPause;
    QAction * toolButtonRepeat;
    QAction * toolButtonShuffle;

    QueuedFunc buffering_timer;

    void closeEvent (QCloseEvent * e);

    void setWindowTitle (const QString & title);

    void updateToggles ();
    void updateVisibility ();
    void readSettings ();

    void add_dock_plugins ();
    void remove_dock_plugins ();

    void update_play_pause ();
    void title_change_cb ();
    void playback_begin_cb ();
    void buffering_cb ();
    void playback_ready_cb ();
    void pause_cb ();
    void playback_stop_cb ();
    void update_toggles_cb ();
    void update_visibility_cb ();

    PluginWidget * find_dock_plugin (PluginHandle * plugin);
    void add_dock_plugin_cb (PluginHandle * plugin);
    void remove_dock_plugin_cb (PluginHandle * plugin);

    const HookReceiver<MainWindow>
     hook1 {"title change", this, & MainWindow::title_change_cb},
     hook2 {"playback begin", this, & MainWindow::playback_begin_cb},
     hook3 {"playback ready", this, & MainWindow::title_change_cb},
     hook4 {"playback pause", this, & MainWindow::pause_cb},
     hook5 {"playback unpause", this, & MainWindow::pause_cb},
     hook6 {"playback stop", this, & MainWindow::playback_stop_cb},
     hook7 {"set repeat", this, & MainWindow::update_toggles_cb},
     hook8 {"set shuffle", this, & MainWindow::update_toggles_cb},
     hook9 {"set no_playlist_advance", this, & MainWindow::update_toggles_cb},
     hook10 {"set stop_after_current_song", this, & MainWindow::update_toggles_cb},
     hook11 {"qtui toggle menubar", this, & MainWindow::update_visibility_cb},
     hook12 {"qtui toggle infoarea", this, & MainWindow::update_visibility_cb},
     hook13 {"qtui toggle statusbar", this, & MainWindow::update_visibility_cb};

    const HookReceiver<MainWindow, PluginHandle *>
     plugin_hook1 {"dock plugin enabled", this, & MainWindow::add_dock_plugin_cb},
     plugin_hook2 {"dock plugin disabled", this, & MainWindow::remove_dock_plugin_cb};

    Index<PluginWidget *> dock_widgets;
    int playing_id = -1;
};

#endif
