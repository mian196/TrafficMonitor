#include "stdafx.h"
#include "HistoryTrafficFile.h"
#include "Common.h"

CHistoryTrafficFile::CHistoryTrafficFile(const wstring& file_path)
	: m_file_path(file_path)
{
}

CHistoryTrafficFile::~CHistoryTrafficFile()
{
}

void CHistoryTrafficFile::Save() const
{
	// Save to a temporary file first to prevent data loss on crash (Atomic Save)
	wstring temp_path = m_file_path + L".tmp";
	ofstream file{ temp_path };
	if (!file.is_open())
		return;

	char buff[64];
	sprintf_s(buff, "lines: \"%u\"", static_cast<unsigned int>(m_history_traffics.size()));			// Write the total number of lines in the first line
	file << buff << std::endl;
	for (const auto& history_traffic : m_history_traffics)
	{
		if (history_traffic.mixed)
			sprintf_s(buff, "%.4d/%.2d/%.2d %llu", history_traffic.year, history_traffic.month, history_traffic.day, history_traffic.down_kBytes);
		else
			sprintf_s(buff, "%.4d/%.2d/%.2d %llu/%llu", history_traffic.year, history_traffic.month, history_traffic.day, history_traffic.up_kBytes, history_traffic.down_kBytes);
		file << buff << std::endl;
	}
	
	bool write_success = file.good();
	file.close();

	// Atomically replace the original file
	if (write_success)
	{
		MoveFileEx(temp_path.c_str(), m_file_path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH | MOVEFILE_COPY_ALLOWED);
	}
	else
	{
		DeleteFile(temp_path.c_str());
	}
}

void CHistoryTrafficFile::Load()
{
	ifstream file{ m_file_path };
	string current_line, temp;
	HistoryTraffic traffic;
	//bool first_line{ true };
	if (CCommon::FileExist(m_file_path.c_str()))
	{
		while (!file.eof())
		{
			if (m_history_traffics.size() > 9999) break;		// Read up to 10000 days of history records
			std::getline(file, current_line);
			//if (first_line)
			//{
			//	first_line = false;
			//	size_t index = current_line.find("lines:");
			//	if(index != wstring::npos)
			//	{
			//		index = current_line.find("\"", index + 6);
			//		size_t index1 = current_line.find("\"", index + 1);
			//		temp = current_line.substr(index + 1, index1 - index - 1);
			//		m_size = atoll(temp.c_str());
			//		continue;
			//	}
			//}

			if (current_line.size() < 12) continue;
			temp = current_line.substr(0, 4);
			traffic.year = atoi(temp.c_str());
			if (traffic.year < 1900 || traffic.year > 3000)
				continue;
			temp = current_line.substr(5, 2);
			traffic.month = atoi(temp.c_str());
			if (traffic.month < 1 || traffic.month > 12)
				continue;
			temp = current_line.substr(8, 2);
			traffic.day = atoi(temp.c_str());
			if (traffic.day < 1 || traffic.day > 31)
				continue;

			int index = current_line.find(L'/', 11);
			traffic.mixed = (index == wstring::npos);
			if (traffic.mixed)
			{
				temp = current_line.substr(11);
				traffic.down_kBytes = atoll(temp.c_str());
				traffic.up_kBytes = 0;
			}
			else
			{
				temp = current_line.substr(11, index - 11);
				traffic.up_kBytes = atoll(temp.c_str());
				temp = current_line.substr(index + 1);
				traffic.down_kBytes = atoll(temp.c_str());
			}
			if (traffic.year > 0 && traffic.month > 0 && traffic.day > 0 && traffic.kBytes() > 0)
				m_history_traffics.push_back(traffic);
		}
	}

	MormalizeData();
}

void CHistoryTrafficFile::LoadSize()
{
	ifstream file{ m_file_path };
	string current_line, temp;
	if (CCommon::FileExist(m_file_path.c_str()))
	{
		std::getline(file, current_line);			// Read the first line
		size_t index = current_line.find("lines:");
		if (index != wstring::npos)
		{
			index = current_line.find("\"", index + 6);
			size_t index1 = current_line.find("\"", index + 1);
			temp = current_line.substr(index + 1, index1 - index - 1);
			m_size = atoll(temp.c_str());
		}
	}
}

void CHistoryTrafficFile::Merge(const CHistoryTrafficFile& history_traffic, bool ignore_same_data)
{
	for (const HistoryTraffic& traffic : history_traffic.m_history_traffics)
	{
		if(ignore_same_data)
		{
			// If ignoring items with the same date, use binary search to find the item with the same date. If found, skip it.
			if (std::binary_search(m_history_traffics.begin(), m_history_traffics.end(), traffic, HistoryTraffic::DateGreater))
			{
				auto iter = std::lower_bound(m_history_traffics.begin(), m_history_traffics.end(), traffic, HistoryTraffic::DateGreater);
				if (iter != m_history_traffics.end())
				{
					continue;
				}
			}
		}
		m_history_traffics.push_back(traffic);
	}
	MormalizeData();
}

void CHistoryTrafficFile::MormalizeData()
{
	SYSTEMTIME current_time;
	GetLocalTime(&current_time);
	HistoryTraffic traffic;
	traffic.year = current_time.wYear;
	traffic.month = current_time.wMonth;
	traffic.day = current_time.wDay;
	traffic.up_kBytes = 0;
	traffic.down_kBytes = 0;
	traffic.mixed = false;

	if (m_history_traffics.empty())
	{
		m_history_traffics.push_front(traffic);
	}

	if (m_history_traffics.size() >= 2)
	{
		// Sort the read history traffic list by date in descending order
		std::sort(m_history_traffics.begin(), m_history_traffics.end(), HistoryTraffic::DateGreater);

		// If there are items with the same date in the list, merge them
		for (int i{}; i < static_cast<int>(m_history_traffics.size() - 1); i++)
		{
			if (HistoryTraffic::DateEqual(m_history_traffics[i], m_history_traffics[i + 1]))
			{
				m_history_traffics[i].up_kBytes += m_history_traffics[i + 1].up_kBytes;
				m_history_traffics[i].down_kBytes += m_history_traffics[i + 1].down_kBytes;
				m_history_traffics.erase(m_history_traffics.begin() + i + 1);
			}
		}
	}

	// If the date of the first item in the list is today, use its traffic as today's usage. Otherwise, insert a new item for today at the front.
	if (HistoryTraffic::DateEqual(m_history_traffics[0], traffic))
	{
		m_today_up_traffic = static_cast<__int64>(m_history_traffics[0].up_kBytes) * 1024;
		m_today_down_traffic = static_cast<__int64>(m_history_traffics[0].down_kBytes) * 1024;
		m_history_traffics[0].mixed = false;
	}
	else
	{
		m_history_traffics.push_front(traffic);
	}
	m_size = m_history_traffics.size();
}
