// SPDX-License-Identifier: MIT
#pragma once

#include "app/Application.h"
#include "app/ModelCatalog.h"

#include <QWidget>

class QButtonGroup;
class QLabel;
class QProgressBar;
class QPushButton;
class QRadioButton;
class QVBoxLayout;

namespace afterline::ui {

// One downloadable model: what it is, its size, and download/remove/use.
class ModelRow : public QWidget {
    Q_OBJECT
public:
    ModelRow(AppContext ctx, const ModelSpec &spec, QButtonGroup *group, QWidget *parent = nullptr);
    void refresh();
    QString id() const { return m_spec.id; }
    QRadioButton *radio() const { return m_radio; }

private:
    AppContext m_ctx;
    ModelSpec m_spec;
    QRadioButton *m_radio;
    QLabel *m_status;
    QProgressBar *m_progress;
    QPushButton *m_download, *m_cancel, *m_remove;
};

// Builds the list of models of one kind, with "use this one" selection.
QWidget *modelList(AppContext ctx, ModelKind kind, QWidget *parent = nullptr);

class SettingsView : public QWidget {
    Q_OBJECT
public:
    explicit SettingsView(AppContext ctx, QWidget *parent = nullptr);

Q_SIGNALS:
    void themeChanged();
    void closeRequested();

private:
    void changeStorage();
    AppContext m_ctx;
};

}  // namespace afterline::ui
