// Copyright (c) 2022 Akihiro Yamamoto.
// Licensed under the MIT License <https://spdx.org/licenses/MIT.html>
// See LICENSE file in the project root for full license information.
//
#pragma once
#include "Gui.hpp"
#include <sstream>

//
class StringBufWithDialogue : public std::stringbuf {
  Widget::Dialogue _dialogue;

public:
  StringBufWithDialogue(const std::string &title) : _dialogue{title} {}
  //
  virtual int sync() {
    std::istringstream iss{this->str()};
    std::string work;
    std::string tail;
    while (std::getline(iss, work)) {
      tail = work;
    }
    _dialogue.setMessage(tail);
    return std::stringbuf::sync();
  }
};
