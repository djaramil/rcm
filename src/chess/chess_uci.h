// Copyright (C) 2024 Eric Sessoms
// See license at end of file

#ifndef CHESS_UCI_H
#define CHESS_UCI_H

struct UCIEngine;

struct UCIEngine *uci_execvp(const char *file, char *const argv[]);
void uci_quit(struct UCIEngine *engine);

enum UCIRequest {
    UCI_REQUEST_QUIT,
    UCI_REQUEST_UCI,
};

struct UCIMessage {
    enum UCIRequest type;
};

struct UCIMessage *uci_receive(struct UCIEngine *engine);
void uci_send(struct UCIEngine *engine, struct UCIMessage *message);

#endif

// This file is part of the Raccoon's Centaur Mods (RCM).
//
// RCM is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// RCM is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.