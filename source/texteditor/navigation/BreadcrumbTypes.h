#pragma once

#include "AlifSymbolTable.h"

#include <QMetaType>
#include <QVector>

/** Cursor-local semantic breadcrumb data derived only from the current analysis revision. */
struct EditorBreadcrumbContext final {
    quint64 revision = 0;
    qsizetype cursorOffset = 0;
    QVector<SemanticBreadcrumb> symbolPath;
};
Q_DECLARE_METATYPE(EditorBreadcrumbContext)
