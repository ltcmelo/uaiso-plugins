/******************************************************************************
 * Copyright (c) 2014-2015 Leandro T. C. Melo (ltcmelo@gmail.com)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
 * USA
 *****************************************************************************/

#ifndef UAISO_QTC_EDITOR_H
#define UAISO_QTC_EDITOR_H

#include <coreplugin/editormanager/ieditorfactory.h>
#include <extensionsystem/iplugin.h>
#include <texteditor/basehoverhandler.h>
#include <texteditor/semantichighlighter.h>
#include <texteditor/syntaxhighlighter.h>
#include <texteditor/textdocument.h>
#include <texteditor/texteditor.h>

#include <QtPlugin>
#include <QAction>
#include <QFutureWatcher>
#include <QScopedPointer>
#include <QTimer>

    /* Uaiso - https://github.com/ltcmelo/uaiso
     *
     * Notice: This implementation has the only purpose of showcasing a few
     * components of the Uaiso project. It's not intended to be efficient,
     * nor to be taken as reference on how to write Qt Creator editors. It's
     * not even complete. The primary concern is to demonstrate Uaiso's API.
     */

#include <Parsing/LangName.h>
#include <Parsing/LexemeMap.h>
#include <Parsing/TokenMap.h>
#include <Semantic/Snapshot.h>

namespace uaiso {

class DiagnosticReports;
class Factory;
class IncrementalLexer;
class Manager;
class Unit;

}

namespace UaisoQtc {

class UaisoSettingsPage;

    //--------------//
    //--- Plugin ---//
    //--------------//

void addSearchPaths(uaiso::Manager* manager, uaiso::LangName);

class UaisoEditorPlugin : public ExtensionSystem::IPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QtCreatorPlugin" FILE "UaisoEditor.json")

public:
    UaisoEditorPlugin();
    ~UaisoEditorPlugin();

    static UaisoEditorPlugin *instance();

    bool initialize(const QStringList &arguments, QString *errorMessage = 0);
    void extensionsInitialized();

    uaiso::LexemeMap *lexemes() { return &m_lexemes; }
    uaiso::TokenMap *tokens() { return &m_tokens; }
    uaiso::Snapshot snapshot() { return m_snapshot; }

    UaisoSettingsPage* settingsPage();

private:
    static UaisoEditorPlugin *m_instance;

    uaiso::LexemeMap m_lexemes;
    uaiso::TokenMap m_tokens;
    uaiso::Snapshot m_snapshot;

    UaisoSettingsPage* m_settingsPage;
};

    //---------------//
    //--- Factory ---//
    //---------------//

class UaisoEditorFactory : public TextEditor::TextEditorFactory
{
    Q_OBJECT

public:
    UaisoEditorFactory();
};

    //----------------//
    //--- Document ---//
    //----------------//

class UaisoEditorDocument : public TextEditor::TextDocument
{
    Q_OBJECT

public:
    explicit UaisoEditorDocument();
    ~UaisoEditorDocument();

    QTimer m_syntaxCheckTimer;
    QTimer m_semanticCheckTimer;
    std::unique_ptr<uaiso::Factory> m_factory;
    std::unique_ptr<uaiso::Unit> m_unit;
    std::unique_ptr<uaiso::DiagnosticReports> m_reports;
    std::unique_ptr<QFutureWatcher<TextEditor::HighlightingResult>> m_watcher;

signals:
    void requestDiagnosticsUpdate();

private slots:
    void configure(const QString &oldPath, const QString &path);
    void updateFontSettings(const TextEditor::FontSettings& fs);

    // Parsing, binding, type-checking.
    void triggerAnalysis();
    void parse();
    void bindAndCheck();
    void processSemanticData();

    // Symbols info.
    void semanticDataAvailable(int from, int to);
    void semanticDataFinished();

private:
    void disconnectWatcher();

    QHash<int, QTextCharFormat> m_kindToFormat;
    int m_semanticRevision;
};

    //--------------//
    //--- Editor ---//
    //--------------//

class UaisoEditor : public TextEditor::BaseTextEditor
{
    Q_OBJECT

public:
    UaisoEditor();
    ~UaisoEditor();
};

    //--------------//
    //--- Widget ---//
    //--------------//

class UaisoEditorWidget : public TextEditor::TextEditorWidget
{
    Q_OBJECT

public:
    UaisoEditorWidget();
    ~UaisoEditorWidget();

    TextEditor::AssistInterface *
    createAssistInterface(TextEditor::AssistKind assistKind,
                          TextEditor::AssistReason reason) const;

    void finalizeInitialization();

public slots:
    void updateDiagnostics();
};

    //------------------------//
    //--- Syntax Highlight ---//
    //------------------------//

class UaisoSyntaxHighlighter : public TextEditor::SyntaxHighlighter
{
    Q_OBJECT

public:
    UaisoSyntaxHighlighter(uaiso::Factory* factory);
    ~UaisoSyntaxHighlighter();

protected:
    void highlightBlock(const QString &text) Q_DECL_OVERRIDE;

    std::unique_ptr<uaiso::IncrementalLexer> m_lexer;
};

    //-------------//
    //--- Hover ---//
    //-------------//

class UaisoHoverHandler : public TextEditor::BaseHoverHandler
{
    Q_OBJECT

public:
    void identifyMatch(TextEditor::TextEditorWidget *widget, int pos);
};

    //-----------------//
    //--- Constants ---//
    //-----------------//

namespace Constants {

const char EDITOR_ID[] = "UaisoEditor.UaisoEditor";
const char EDITOR_DISPLAY_NAME[] = QT_TRANSLATE_NOOP("OpenWith::Editors",
                                                     "Uaiso Editor");
const char MIMETYPE_XML[] = ":/uaiso/UaisoEditor.mimetypes.xml";
const char D_MIMETYPE[]    = "text/x-dsrc";
const char GO_MIMETYPE[]   = "text/x-gosrc";
const char PY_MIMETYPE[]   = "text/x-python";
const char RUST_MIMETYPE[] = "text/x-rustsrc";

const char SETTINGS_ID[] = "AA.Uaiso Configurations";
const char SETTINGS_CATEGORY[] = "AA.Uaiso";
const char SETTINGS_TR_CATEGORY[] = QT_TRANSLATE_NOOP("Uaiso", "Uaiso");
const char SETTINGS_CATEGORY_ICON[] = ":/uaiso/images/uaiso.png";

} // namespace Constants

} // namespace UaisoQtc

#endif
