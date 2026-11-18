/*
*   Copyright (C) 2016 by Jonathan Naylor G4KLX
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

#include "DMRLookup.h"
#include "Timer.h"
#include "Log.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <cerrno>

CDMRLookup::CDMRLookup(const std::string& filename, unsigned int reloadTime) :
CThread(),
m_filename(filename),
m_reloadTime(reloadTime),
m_table(),
m_mutex(),
m_stop(false)
{
}

CDMRLookup::~CDMRLookup()
{
}

bool CDMRLookup::read()
{
	bool ret = load();

	if (m_reloadTime > 0U)
		run();

	return ret;
}

void CDMRLookup::entry()
{
	LogInfo("Started the DMR Id lookup reload thread");

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

	LogInfo("Stopped the DMR Id lookup reload thread");
}

void CDMRLookup::stop()
{
	if (m_reloadTime == 0U) {
		delete this;
		return;
	}

	m_stop = true;

	wait();
}

std::string CDMRLookup::find(unsigned int id)
{
	std::string callsign;

	if (id == 0xFFFFFFU)
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

bool CDMRLookup::load()
{
        FILE* fp = ::fopen(m_filename.c_str(), "rt");
        if (fp == NULL) {
                LogWarning("Cannot open the Id lookup file - %s", m_filename.c_str());
                return false;
        }

        std::unordered_map<unsigned int, std::string> newTable;

        char buffer[256U];
        bool invalid = false;

        while (::fgets(buffer, sizeof(buffer), fp) != NULL) {
                if (buffer[0U] == '#' || buffer[0U] == '\n')
                        continue;

                // Accept short final lines without a newline.
                // Reject records that fill or exceed the buffer.
                if (::strchr(buffer, '\n') == NULL) {
                        if (::strlen(buffer) >= sizeof(buffer) - 1U) {
                                invalid = true;
                                break;
                        }
                }

                char* p1 = ::strtok(buffer, " ;\t\r\n");
                char* p2 = ::strtok(NULL, " ;\t\r\n");

                if (p1 == NULL || p2 == NULL) {
                        invalid = true;
                        break;
                }

                for (const char* p = p1; *p != '\0'; p++) {
                        if (*p < '0' || *p > '9') {
                                invalid = true;
                                break;
                        }
                }

                if (invalid)
                        break;

                errno = 0;
                char* end = NULL;
                unsigned long id = ::strtoul(p1, &end, 10);

                if (errno == ERANGE || *end != '\0' ||
                    id == 0UL || id > 0xFFFFFFFFUL) {
                        invalid = true;
                        break;
                }

                for (char* p = p2; *p != '\0'; p++) {
                        unsigned char c = (unsigned char)*p;

                        if (!::isalnum(c) && c != '/' && c != '-') {
                                invalid = true;
                                break;
                        }

                        *p = (char)::toupper(c);
                }

                if (invalid)
                        break;

                if (!newTable.emplace(
                        (unsigned int)id, std::string(p2)).second) {
                        invalid = true;
                        break;
                }
        }

        if (::ferror(fp))
                invalid = true;

        ::fclose(fp);

        if (invalid || newTable.empty()) {
                LogWarning("Invalid or empty Id lookup file - %s",
                           m_filename.c_str());
                return false;
        }

        size_t size = newTable.size();

        m_mutex.lock();
        m_table.swap(newTable);
        m_mutex.unlock();

        LogInfo("Loaded %u Ids to the callsign lookup table",
                (unsigned int)size);

        return true;
}
