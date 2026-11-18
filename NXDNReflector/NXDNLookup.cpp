/*
*   Copyright (C) 2016,2017,2018 by Jonathan Naylor G4KLX
*
*   This program is free software; you can redistribute it and/or modify
*   it under the terms of the GNU General Public License as published by
*   the Free Software Foundation; either version 2 of the License, or
*   (at your option) any later version.
*
*   This program is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*   GNU General Public License for more details.
*
*   You should have received a copy of the GNU General Public License
*   along with this program; if not, write to the Free Software
*   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include "NXDNLookup.h"
#include "Timer.h"
#include "Log.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <cerrno>
#include <fstream>

CNXDNLookup::CNXDNLookup(const std::string& filename, unsigned int reloadTime) :
CThread(),
m_filename(filename),
m_reloadTime(reloadTime),
m_table(),
m_mutex(),
m_stop(false)
{
}

CNXDNLookup::~CNXDNLookup()
{
}

bool CNXDNLookup::read()
{
	bool ret = load();

	if (m_reloadTime > 0U)
		run();

	return ret;
}

void CNXDNLookup::entry()
{
	LogInfo("Started the NXDN Id lookup reload thread");

	CTimer timer(1U, 3600U * m_reloadTime);
	timer.start();

	while (!m_stop) {
		sleep(1000U);

		timer.clock();
		if (timer.hasExpired()) {
			load();
			timer.start();
		}
	}

	LogInfo("Stopped the NXDN Id lookup reload thread");
}

void CNXDNLookup::stop()
{
	if (m_reloadTime == 0U) {
		delete this;
		return;
	}

	m_stop = true;

	wait();
}

std::string CNXDNLookup::find(unsigned int id)
{
	std::string callsign;

	if (id == 0xFFFFU)
		return std::string("ALL");

	m_mutex.lock();

	try {
		callsign = m_table.at(id);
	} catch (...) {
		char text[10U];
		::sprintf(text, "%u", id);
		callsign = std::string(text);
	}

	m_mutex.unlock();

	return callsign;
}

bool CNXDNLookup::exists(unsigned int id)
{
	m_mutex.lock();

	bool found = m_table.count(id) == 1U;

	m_mutex.unlock();

	return found;
}

bool CNXDNLookup::load()
{
        std::ifstream file(m_filename.c_str());
        if (!file.is_open()) {
                LogWarning("Cannot open the NXDN Id lookup file - %s",
                           m_filename.c_str());
                return false;
        }

        std::unordered_map<unsigned int, std::string> newTable;
        std::string line;
        bool invalid = false;
        bool headerSeen = false;

        while (std::getline(file, line)) {
                if (!line.empty() && line.back() == '\r')
                        line.pop_back();

                if (line.empty() || line[0U] == '#')
                        continue;

                if (line.size() > 4096U) {
                        invalid = true;
                        break;
                }

                std::size_t comma1 = line.find(',');
                if (comma1 == std::string::npos) {
                        invalid = true;
                        break;
                }

                std::size_t comma2 = line.find(',', comma1 + 1U);

                std::string idText = line.substr(0U, comma1);
                std::string callsign = line.substr(
                    comma1 + 1U,
                    comma2 == std::string::npos
                        ? std::string::npos
                        : comma2 - comma1 - 1U);

                if (idText == "RADIO_ID" &&
                    callsign == "CALLSIGN") {
                        if (headerSeen || !newTable.empty()) {
                                invalid = true;
                                break;
                        }
                        headerSeen = true;
                        continue;
                }

                if (idText.empty() || callsign.empty()) {
                        invalid = true;
                        break;
                }

                for (char c : idText) {
                        if (c < '0' || c > '9') {
                                invalid = true;
                                break;
                        }
                }

                if (invalid)
                        break;

                errno = 0;
                char* end = NULL;
                unsigned long id = ::strtoul(idText.c_str(), &end, 10);

                if (errno == ERANGE || *end != '\0' ||
                    id == 0UL || id > 65535UL) {
                        invalid = true;
                        break;
                }

                for (char& c : callsign) {
                        unsigned char ch = (unsigned char)c;
                        if (!::isalnum(ch) && ch != '/' && ch != '-') {
                                invalid = true;
                                break;
                        }
                        c = (char)::toupper(ch);
                }

                if (invalid)
                        break;

                if (!newTable.emplace((unsigned int)id, callsign).second) {
                        invalid = true;
                        break;
                }
        }

        if (file.bad())
                invalid = true;

        if (invalid || newTable.empty()) {
                LogWarning("Invalid or empty NXDN Id lookup file - %s",
                           m_filename.c_str());
                return false;
        }

        size_t size = newTable.size();

        m_mutex.lock();
        m_table.swap(newTable);
        m_mutex.unlock();

        LogInfo("Loaded %u Ids to the NXDN callsign lookup table",
                (unsigned int)size);

        return true;
}
