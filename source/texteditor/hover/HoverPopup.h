#pragma once

#include "SemanticHoverProvider.h"

#include <QFrame>

class QLabel;
class QPaintEvent;

/**
 * Compact, non-focusable RTL surface for semantic hover information. It mirrors
 * the completion popup's dark visual system without sharing QCompleter state.
 */
class THoverPopup final : public QFrame {
public:
    explicit THoverPopup(QWidget* parent = nullptr);

    void setHoverInfo(const HoverInfo& info);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QLabel* iconLabel{};
    QLabel* headerLabel{};
    QLabel* metadataLabel{};
    QLabel* documentationLabel{};
};
