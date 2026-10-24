#pragma once
#include <QCoreApplication>
#include <QString>

// One door for wording that C++ hands to the interface, 2026-10-24.
//
// A status line, a queue row's reason, a sheet's error sentence: all of it is
// wording, and wording belongs to the language file, not to a .cpp.  The
// Chinese lives in i18n/zh_CN.tsv, becomes i18n/zuuned_zh_CN.ts, and is
// compiled into a .qm that a running Zuuned loads at start - see
// installUiLanguage() in src/main.cpp - so re-wording the interface is a file
// edit and a restart rather than a build.  A sentence spelled straight into a
// QML property or a QVariantMap is a sentence that stays English forever.
//
// Why a struct and not tr(): the call sites are free functions, static members
// and lambdas, where QObject's protected tr() is not reachable, and a macro
// would be invisible to lupdate, which parses the text instead of
// preprocessing it.  lupdate reads `UiText::tr("...")` as a tr() call on a
// class named UiText and files the string under the context "UiText";
// translate() below asks for that same context, so extraction and lookup agree
// by construction.  Rename the struct without renaming the context and every
// one of those strings falls back to English on its own.
//
// lupdate prints "Class 'UiText' lacks Q_OBJECT macro" on every run.  That is
// the sound of this working: the struct is a key namespace, not a QObject, and
// it stays header-only so any translation unit can use it.
//
// The argument must be a literal.  A string built at runtime cannot be looked
// up because nothing extracted it, which is exactly the rule this file exists
// to make impossible to break quietly.
struct UiText {
    static inline QString tr(const char *source)
    {
        return QCoreApplication::translate("UiText", source);
    }
};
