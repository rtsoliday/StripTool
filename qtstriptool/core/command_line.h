#pragma once

#include "core/model.h"
#include <QCommandLineParser>

namespace striptool {

void configureCommandLineParser(QCommandLineParser& parser);
bool applyCommandLineTiming(const QCommandLineParser& parser,
                           StripToolModel& model, QString* error = nullptr);

}  // namespace striptool
