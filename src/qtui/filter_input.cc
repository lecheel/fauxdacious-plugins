/*
 * filter_input.cc
 * Copyright 2014 Daniel (dmilith) Dettlaff
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

#include <libaudcore/i18n.h>

#include "filter_input.h"

#include <QKeyEvent>

FilterInput::FilterInput (QWidget * parent) : QLineEdit (parent)
{
#ifdef Q_OS_MAC
    setStyleSheet (
        "QLineEdit {"
        "   padding: 2px 4px;"
        "   border: 1px solid silver;"
        "   border-radius: 10px;"
        "   margin-right: 5px;"
        "}"
        "QLineEdit:focus {"
        "   border: 1px solid gray;"
        "}"
    );
#endif

    setAttribute (Qt::WA_MacShowFocusRect, false);
    setClearButtonEnabled (true);
    setPlaceholderText (_("Search"));
}

void FilterInput::keyPressEvent (QKeyEvent * e)
{
    if (e->key () == Qt::Key_Enter || e->key () == Qt::Key_Return)
    {
        e->ignore ();
        focusNextChild ();
    }
    else
        QLineEdit::keyPressEvent (e);
}
