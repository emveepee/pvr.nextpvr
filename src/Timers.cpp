/*
 *  Copyright (C) 2020-2023 Team Kodi (https://kodi.tv)
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "Timers.h"
#include "utilities/XMLUtils.h"

#include "pvrclient-nextpvr.h"
#include <kodi/General.h>
#include <kodi/tools/StringUtils.h>
#include <string>

using namespace NextPVR;
using namespace NextPVR::utilities;

/************************************************************/
/** Timer handling */

Timers::Timers(const std::shared_ptr<InstanceSettings>& settings, Request& request, Channels& channels, cPVRClientNextPVR& pvrclient) :
  m_settings(settings),
  m_request(request),
  m_channels(channels),
  m_pvrclient(pvrclient)
{
}

PVR_ERROR Timers::GetTimersAmount(int& amount)
{
  if (m_iTimerCount != -1)
  {
    amount = m_iTimerCount;
    return PVR_ERROR_NO_ERROR;
  }
  int timerCount = -1;
  // get list of recurring recordings
  tinyxml2::XMLDocument doc;
  if (m_request.DoMethodRequest("recording.recurring.list", doc) == tinyxml2::XML_SUCCESS)
  {
    tinyxml2::XMLNode* recordingsNode = doc.RootElement()->FirstChildElement("recurrings");
    if (recordingsNode != nullptr)
    {
      tinyxml2::XMLNode* pRecordingNode;
      for (pRecordingNode = recordingsNode->FirstChildElement("recurring"); pRecordingNode; pRecordingNode = pRecordingNode->NextSiblingElement())
      {
        timerCount++;
      }
    }
  }
  // get list of pending recordings
  doc.Clear();
  if (m_request.DoMethodRequest("recording.list&filter=pending", doc) == tinyxml2::XML_SUCCESS)
  {
    tinyxml2::XMLNode* recordingsNode = doc.RootElement()->FirstChildElement("recordings");
    if (recordingsNode != nullptr)
    {
      tinyxml2::XMLNode* pRecordingNode;
      for (pRecordingNode = recordingsNode->FirstChildElement("recording"); pRecordingNode; pRecordingNode = pRecordingNode->NextSiblingElement())
      {
        timerCount++;
      }
    }
  }
  if (timerCount > -1)
  {
    // to do why?
    m_iTimerCount = timerCount + 1;
  }
  amount = m_iTimerCount;
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR Timers::GetTimers(kodi::addon::PVRTimersResultSet& results)
{
  PVR_ERROR returnValue = PVR_ERROR_NO_ERROR;
  int timerCount = 0;
  // first add the recurring recordings
  tinyxml2::XMLDocument doc;
  if (m_request.DoMethodRequest("recording.recurring.list", doc) == tinyxml2::XML_SUCCESS)
  {
    tinyxml2::XMLNode* recurringsNode = doc.RootElement()->FirstChildElement("recurrings");
    tinyxml2::XMLNode* pRecurringNode;
    if (m_settings->m_backendVersion >= NEXTPVR_VERSION_PRIORITY)
      InitializePriorities(recurringsNode);
    for (pRecurringNode = recurringsNode->FirstChildElement("recurring"); pRecurringNode; pRecurringNode = pRecurringNode->NextSiblingElement())
    {
      kodi::addon::PVRTimer tag;
      tinyxml2::XMLNode* pMatchRulesNode = pRecurringNode->FirstChildElement("matchrules");
      tinyxml2::XMLNode* pRulesNode = pMatchRulesNode->FirstChildElement("Rules");

      tag.SetClientIndex(XMLUtils::GetUIntValue(pRecurringNode, "id"));
      int channelUID = XMLUtils::GetIntValue(pRulesNode, "ChannelOID");
      if (channelUID == 0)
      {
        tag.SetClientChannelUid(PVR_TIMER_ANY_CHANNEL);
      }
      else if (m_channels.m_channelDetails.find(channelUID) == m_channels.m_channelDetails.end())
      {
        kodi::Log(ADDON_LOG_DEBUG, "Invalid channel uid %d", channelUID);
        tag.SetClientChannelUid(PVR_CHANNEL_INVALID_UID);
      }
      else
      {
        tag.SetClientChannelUid(channelUID);
      }
      tag.SetTimerType(pRulesNode->FirstChildElement("EPGTitle") ? TIMER_REPEATING_EPG : TIMER_REPEATING_MANUAL);

      std::string buffer;

      // start/end time

      const int recordingType = XMLUtils::GetUIntValue(pRecurringNode, "type");

      if (recordingType == 1 || recordingType == 2)
      {
        tag.SetStartTime(TIMER_DATE_MIN);
        tag.SetEndTime(TIMER_DATE_MIN);
        tag.SetStartAnyTime(true);
        tag.SetEndAnyTime(true);
      }
      else
      {
        if (XMLUtils::GetString(pRulesNode, "StartTimeTicks", buffer))
          tag.SetStartTime(stoll(buffer));
        if (XMLUtils::GetString(pRulesNode, "EndTimeTicks", buffer))
          tag.SetEndTime(stoll(buffer));
        if (recordingType == 7)
        {
          tag.SetEPGSearchString(TYPE_7_TITLE);
        }
      }

      // keyword recordings
      std::string advancedRulesText;
      if (XMLUtils::GetString(pRulesNode, "AdvancedRules", advancedRulesText))
      {
        if (advancedRulesText.find("KEYWORD: ") != std::string::npos)
        {
          tag.SetTimerType(TIMER_REPEATING_KEYWORD);
          tag.SetStartTime(TIMER_DATE_MIN);
          tag.SetEndTime(TIMER_DATE_MIN);
          tag.SetStartAnyTime(true);
          tag.SetEndAnyTime(true);
          tag.SetEPGSearchString(advancedRulesText.substr(9));
        }
        else
        {
          tag.SetTimerType(TIMER_REPEATING_ADVANCED);
          tag.SetStartTime(TIMER_DATE_MIN);
          tag.SetEndTime(TIMER_DATE_MIN);
          tag.SetStartAnyTime(true);
          tag.SetEndAnyTime(true);
          tag.SetFullTextEpgSearch(true);
          tag.SetEPGSearchString(advancedRulesText);
        }
      }

      // days
      tag.SetWeekdays(PVR_WEEKDAY_ALLDAYS);
      std::string daysText;
      if (XMLUtils::GetString(pRulesNode, "Days", daysText))
      {
        unsigned int weekdays = PVR_WEEKDAY_NONE;
        if (daysText.find("SUN") != std::string::npos)
          weekdays |= PVR_WEEKDAY_SUNDAY;
        if (daysText.find("MON") != std::string::npos)
          weekdays |= PVR_WEEKDAY_MONDAY;
        if (daysText.find("TUE") != std::string::npos)
          weekdays |= PVR_WEEKDAY_TUESDAY;
        if (daysText.find("WED") != std::string::npos)
          weekdays |= PVR_WEEKDAY_WEDNESDAY;
        if (daysText.find("THU") != std::string::npos)
          weekdays |= PVR_WEEKDAY_THURSDAY;
        if (daysText.find("FRI") != std::string::npos)
          weekdays |= PVR_WEEKDAY_FRIDAY;
        if (daysText.find("SAT") != std::string::npos)
          weekdays |= PVR_WEEKDAY_SATURDAY;
        tag.SetWeekdays(weekdays);
      }

      // pre/post padding
      tag.SetMarginStart(XMLUtils::GetUIntValue(pRulesNode, "PrePadding"));
      tag.SetMarginEnd(XMLUtils::GetUIntValue(pRulesNode, "PostPadding"));

      // number of recordings to keep
      tag.SetMaxRecordings(XMLUtils::GetIntValue(pRulesNode, "Keep"));

      // prevent duplicates
      bool duplicate;
      if (XMLUtils::GetBoolean(pRulesNode, "OnlyNewEpisodes", duplicate))
      {
        if (duplicate == true)
        {
          tag.SetPreventDuplicateEpisodes(1);
        }
      }

      std::string recordingDirectoryID;
      if (XMLUtils::GetString(pRulesNode, "RecordingDirectoryID", recordingDirectoryID))
      {
        for (unsigned int i = 0; i < m_settings->m_recordingDirectories.size(); ++i)
        {
          std::string bracketed = "[" + m_settings->m_recordingDirectories[i] + "]";
          if (bracketed == recordingDirectoryID)
          {
            tag.SetRecordingGroup(i);
            break;
          }
        }
      }

      buffer.clear();
      XMLUtils::GetString(pRecurringNode, "name", buffer);
      tag.SetTitle(buffer);
      bool state = true;
      XMLUtils::GetBoolean(pMatchRulesNode, "enabled", state);
      if (state == false)
          tag.SetState(PVR_TIMER_STATE_DISABLED);
      else
          tag.SetState(PVR_TIMER_STATE_SCHEDULED);
      tag.SetSummary("summary");

      if (m_settings->m_backendVersion >= NEXTPVR_VERSION_PRIORITY)
      {
        int priority = XMLUtils::GetUIntValue(pRecurringNode, "priority", -1);
        if (priority >= 500000)
        {
          kodi::Log(ADDON_LOG_INFO, "Skipped timer by priority %s %d %d", tag.GetTitle().c_str(), priority, tag.GetClientIndex());
          continue;
        }
        else
        {
          int priorityClass = std::get<RECURRING_PRIORITY_MAP_CLASS>(m_recurringPriorities[priority]);
          tag.SetPriority(priorityClass);
          //kodi::Log(ADDON_LOG_DEBUG, "timer priority %s %d %d", tag.GetTitle().c_str(), priority, tag.GetPriority());
        }
      }

      // pass timer to xbmc
      timerCount++;
      results.Add(tag);
    }
    // next add the one-off recordings.
    bool isRecordingUpdated = false;
    doc.Clear();
    if (m_request.DoMethodRequest("recording.list&filter=pending", doc) == tinyxml2::XML_SUCCESS)
    {
      tinyxml2::XMLNode* recordingsNode = doc.RootElement()->FirstChildElement("recordings");
      for (tinyxml2::XMLNode* pRecordingNode = recordingsNode->FirstChildElement("recording"); pRecordingNode; pRecordingNode = pRecordingNode->NextSiblingElement())
      {
        kodi::addon::PVRTimer tag;
        UpdatePvrTimer(pRecordingNode, tag);
        // pass timer to xbmc
        timerCount++;
        if (tag.GetState() == PVR_TIMER_STATE_RECORDING)
          isRecordingUpdated = true;
        results.Add(tag);
      }
    }
    doc.Clear();
    if (m_request.DoMethodRequest("recording.list&filter=conflict", doc) == tinyxml2::XML_SUCCESS)
    {
     tinyxml2::XMLNode* recordingsNode = doc.RootElement()->FirstChildElement("recordings");
     for (tinyxml2::XMLNode* pRecordingNode = recordingsNode->FirstChildElement("recording"); pRecordingNode; pRecordingNode = pRecordingNode->NextSiblingElement())
     {
       kodi::addon::PVRTimer tag;
       UpdatePvrTimer(pRecordingNode, tag);
       // pass timer to xbmc
       timerCount++;
       results.Add(tag);
     }
     m_iTimerCount = timerCount;
    }

    if (isRecordingUpdated) {
      m_pvrclient.TriggerRecordingUpdate();
      m_lastTimerUpdateTime = time(nullptr);
    } else if (m_pvrclient.m_nowPlaying == NotPlaying)
      m_lastTimerUpdateTime = time(nullptr);
    // else unknown recording state during playback

  }
  else
  {
    returnValue = PVR_ERROR_SERVER_ERROR;
  }
  return returnValue;
}

bool Timers::UpdatePvrTimer(tinyxml2::XMLNode* pRecordingNode, kodi::addon::PVRTimer& tag)
{
  tag.SetTimerType(pRecordingNode->FirstChildElement("epg_event_oid") ? TIMER_ONCE_EPG : TIMER_ONCE_MANUAL);
  tag.SetClientIndex(XMLUtils::GetUIntValue(pRecordingNode, "id"));
  tag.SetClientChannelUid(XMLUtils::GetUIntValue(pRecordingNode, "channel_id"));
  tag.SetParentClientIndex(XMLUtils::GetUIntValue(pRecordingNode, "recurring_parent", PVR_TIMER_NO_PARENT));

  if (tag.GetParentClientIndex() != PVR_TIMER_NO_PARENT)
  {
    if (tag.GetTimerType() == TIMER_ONCE_EPG)
      tag.SetTimerType(TIMER_ONCE_EPG_CHILD);
    else
      tag.SetTimerType(TIMER_ONCE_MANUAL_CHILD);
  }

  tag.SetMarginStart(XMLUtils::GetUIntValue(pRecordingNode, "pre_padding"));
  tag.SetMarginEnd(XMLUtils::GetUIntValue(pRecordingNode, "post_padding"));

  std::string buffer;

  // name
  XMLUtils::GetString(pRecordingNode, "name", buffer);
  tag.SetTitle(buffer);
  buffer.clear();
  XMLUtils::GetString(pRecordingNode, "desc", buffer);
  tag.SetSummary(buffer);
  // start/end time
  buffer.clear();
  XMLUtils::GetString(pRecordingNode, "start_time_ticks", buffer);
  buffer.resize(10);
  tag.SetStartTime(std::stoll(buffer));
  buffer.clear();
  XMLUtils::GetString(pRecordingNode, "duration_seconds", buffer);
  tag.SetEndTime(tag.GetStartTime() + std::stoll(buffer));

  if (tag.GetTimerType() == TIMER_ONCE_EPG || tag.GetTimerType() == TIMER_ONCE_EPG_CHILD)
  {
    tag.SetEPGUid(XMLUtils::GetUIntValue(pRecordingNode, "epg_end_time_ticks", PVR_TIMER_NO_EPG_UID));

    // version 4 and some versions of v5 won't support the epg end time
    if (tag.GetEPGUid() == PVR_TIMER_NO_EPG_UID)
      tag.SetEPGUid(tag.GetEndTime());

    if (tag.GetEPGUid() != PVR_TIMER_NO_EPG_UID)
    {
      kodi::Log(ADDON_LOG_DEBUG, "Setting timer epg id %d %d", tag.GetClientIndex(), tag.GetEPGUid());
    }

  }

  tag.SetState(PVR_TIMER_STATE_SCHEDULED);

  std::string status;
  if (XMLUtils::GetString(pRecordingNode, "status", status))
  {
    if (status == "Recording" || (status == "Pending" && tag.GetStartTime() <= time(nullptr) + m_settings->m_serverTimeOffset))
    {
      tag.SetState(PVR_TIMER_STATE_RECORDING);
    }
    else if (status == "Conflict")
    {
      tag.SetState(PVR_TIMER_STATE_CONFLICT_NOK);
    }
  }

  if (status == "Pending")
  {
    std::string directory;
    if (XMLUtils::GetString(pRecordingNode, "directory", directory))
    {
        for (unsigned int i = 0; i < m_settings->m_recordingDirectories.size(); ++i)
        {
          if (directory == m_settings->m_recordingDirectories[i])
          {
            tag.SetRecordingGroup(i);
            break;
          }
        }
    }
  }

  return true;
}

namespace
{
  struct TimerType : kodi::addon::PVRTimerType
  {
    TimerType(unsigned int id,
              unsigned int attributes,
              const std::string& description = std::string(),
              const std::vector<kodi::addon::PVRTypeIntValue>& priorityValues =  std::vector<kodi::addon::PVRTypeIntValue>(),
              int priorityDefault = -1,
              const std::vector<kodi::addon::PVRTypeIntValue>& maxRecordingsValues = std::vector<kodi::addon::PVRTypeIntValue>(),
              const int maxRecordingsDefault = 0,
              const std::vector<kodi::addon::PVRTypeIntValue>& dupEpisodesValues = std::vector<kodi::addon::PVRTypeIntValue>(),
              int dupEpisodesDefault = 0,
              const std::vector<kodi::addon::PVRTypeIntValue>& recordingGroupsValues =  std::vector<kodi::addon::PVRTypeIntValue>(),
              int recordingGroupDefault = 0,
              int option = 0
              )
              //const std::vector<kodi::addon::PVRTypeIntValue>& preventDuplicatesValues = std::vector<kodi::addon::PVRTypeIntValue>())
    {
      SetId(id);
      SetAttributes(attributes);
      if (attributes & PVR_TIMER_TYPE_SUPPORTS_PRIORITY)
      {
        SetPriorities(priorityValues);
      }
      SetPrioritiesDefault(priorityDefault);
      SetMaxRecordings(maxRecordingsValues, maxRecordingsDefault);
      SetPreventDuplicateEpisodes(dupEpisodesValues, dupEpisodesDefault);
      SetRecordingGroups(recordingGroupsValues, recordingGroupDefault);
      SetDescription(description);
    }
  };

} // unnamed namespace

PVR_ERROR Timers::GetTimerTypes(std::vector<kodi::addon::PVRTimerType>& types)
{
  static const int MSG_ONETIME_MANUAL = 30140;
  static const int MSG_ONETIME_GUIDE = 30141;
  static const int MSG_REPEATING_MANUAL = 30142;
  static const int MSG_REPEATING_GUIDE = 30143;
  static const int MSG_REPEATING_CHILD = 30144;
  static const int MSG_REPEATING_KEYWORD = 30145;
  static const int MSG_REPEATING_ADVANCED = 30171;

  static const int MSG_KEEPALL = 30150;
  static const int MSG_KEEP1 = 30151;
  static const int MSG_KEEP2 = 30152;
  static const int MSG_KEEP3 = 30153;
  static const int MSG_KEEP4 = 30154;
  static const int MSG_KEEP5 = 30155;
  static const int MSG_KEEP6 = 30156;
  static const int MSG_KEEP7 = 30157;
  static const int MSG_KEEP10 = 30158;

  static const int MSG_SHOWTYPE_FIRSTRUNONLY = 30160;
  static const int MSG_SHOWTYPE_ANY = 30161;

  /* PVR_Timer.iMaxRecordings values and presentation. */
  static std::vector<kodi::addon::PVRTypeIntValue> recordingLimitValues;
  if (recordingLimitValues.size() == 0)
  {
    recordingLimitValues.emplace_back(kodi::addon::PVRTypeIntValue(NEXTPVR_LIMIT_ASMANY, kodi::addon::GetLocalizedString(MSG_KEEPALL)));
    recordingLimitValues.emplace_back(kodi::addon::PVRTypeIntValue(NEXTPVR_LIMIT_1, kodi::addon::GetLocalizedString(MSG_KEEP1)));
    recordingLimitValues.emplace_back(kodi::addon::PVRTypeIntValue(NEXTPVR_LIMIT_2, kodi::addon::GetLocalizedString(MSG_KEEP2)));
    recordingLimitValues.emplace_back(kodi::addon::PVRTypeIntValue(NEXTPVR_LIMIT_3, kodi::addon::GetLocalizedString(MSG_KEEP3)));
    recordingLimitValues.emplace_back(kodi::addon::PVRTypeIntValue(NEXTPVR_LIMIT_4, kodi::addon::GetLocalizedString(MSG_KEEP4)));
    recordingLimitValues.emplace_back(kodi::addon::PVRTypeIntValue(NEXTPVR_LIMIT_5, kodi::addon::GetLocalizedString(MSG_KEEP5)));
    recordingLimitValues.emplace_back(kodi::addon::PVRTypeIntValue(NEXTPVR_LIMIT_6, kodi::addon::GetLocalizedString(MSG_KEEP6)));
    recordingLimitValues.emplace_back(kodi::addon::PVRTypeIntValue(NEXTPVR_LIMIT_7, kodi::addon::GetLocalizedString(MSG_KEEP7)));
    recordingLimitValues.emplace_back(kodi::addon::PVRTypeIntValue(NEXTPVR_LIMIT_10, kodi::addon::GetLocalizedString(MSG_KEEP10)));
  }

  /* PVR_Timer.iPreventDuplicateEpisodes values and presentation.*/
  static std::vector<kodi::addon::PVRTypeIntValue> showTypeValues;
  if (showTypeValues.size() == 0)
  {
    showTypeValues.emplace_back(kodi::addon::PVRTypeIntValue(NEXTPVR_SHOWTYPE_FIRSTRUNONLY, kodi::addon::GetLocalizedString(MSG_SHOWTYPE_FIRSTRUNONLY)));
    showTypeValues.emplace_back(kodi::addon::PVRTypeIntValue(NEXTPVR_SHOWTYPE_ANY, kodi::addon::GetLocalizedString(MSG_SHOWTYPE_ANY)));
  }

  /* PVR_Timer.iRecordingGroup values and presentation */
  static std::vector<kodi::addon::PVRTypeIntValue> recordingGroupValues;
  if (recordingGroupValues.size() == 0)
  {
    for (unsigned int i = 0; i < m_settings->m_recordingDirectories.size(); ++i)
    {
      recordingGroupValues.emplace_back(kodi::addon::PVRTypeIntValue(i, m_settings->m_recordingDirectories[i]));
    }
  }

  static const unsigned int PRIORITY_PVR_IS_REPEATING = m_settings->m_backendVersion >= NEXTPVR_VERSION_PRIORITY ? PVR_TIMER_TYPE_IS_REPEATING | PVR_TIMER_TYPE_SUPPORTS_PRIORITY : PVR_TIMER_TYPE_IS_REPEATING;

  static const unsigned int TIMER_MANUAL_ATTRIBS
    = PVR_TIMER_TYPE_IS_MANUAL |
      PVR_TIMER_TYPE_SUPPORTS_CHANNELS |
      PVR_TIMER_TYPE_SUPPORTS_START_TIME |
      PVR_TIMER_TYPE_SUPPORTS_END_TIME |
      PVR_TIMER_TYPE_SUPPORTS_RECORDING_GROUP |
      PVR_TIMER_TYPE_SUPPORTS_START_END_MARGIN;

  static const unsigned int TIMER_EPG_ATTRIBS
    = PVR_TIMER_TYPE_REQUIRES_EPG_TAG_ON_CREATE |
      PVR_TIMER_TYPE_SUPPORTS_CHANNELS |
      PVR_TIMER_TYPE_SUPPORTS_RECORDING_GROUP |
      PVR_TIMER_TYPE_SUPPORTS_START_END_MARGIN;

  static const unsigned int TIMER_REPEATING_MANUAL_ATTRIBS
    = PRIORITY_PVR_IS_REPEATING |
      static_cast<int>(PVR_TIMER_TYPE_SUPPORTS_ENABLE_DISABLE) |
      PVR_TIMER_TYPE_SUPPORTS_START_END_MARGIN |
      PVR_TIMER_TYPE_SUPPORTS_RECORDING_GROUP |
      PVR_TIMER_TYPE_SUPPORTS_WEEKDAYS |
      PVR_TIMER_TYPE_SUPPORTS_MAX_RECORDINGS;

  static const unsigned int TIMER_REPEATING_EPG_ATTRIBS
    = PRIORITY_PVR_IS_REPEATING |
      static_cast<int>(PVR_TIMER_TYPE_SUPPORTS_ENABLE_DISABLE) |
      PVR_TIMER_TYPE_SUPPORTS_START_END_MARGIN |
      PVR_TIMER_TYPE_SUPPORTS_WEEKDAYS |
      PVR_TIMER_TYPE_SUPPORTS_RECORDING_GROUP |
      PVR_TIMER_TYPE_SUPPORTS_RECORD_ONLY_NEW_EPISODES |
      PVR_TIMER_TYPE_SUPPORTS_ANY_CHANNEL |
      PVR_TIMER_TYPE_SUPPORTS_MAX_RECORDINGS;

  static const unsigned int TIMER_CHILD_ATTRIBUTES
    = PVR_TIMER_TYPE_SUPPORTS_START_TIME |
      PVR_TIMER_TYPE_SUPPORTS_END_TIME |
      PVR_TIMER_TYPE_FORBIDS_NEW_INSTANCES;

  static const unsigned int TIMER_KEYWORD_ATTRIBS
    = PVR_TIMER_TYPE_SUPPORTS_CHANNELS |
      PVR_TIMER_TYPE_SUPPORTS_TITLE_EPG_MATCH |
      PVR_TIMER_TYPE_SUPPORTS_RECORDING_GROUP |
      PVR_TIMER_TYPE_SUPPORTS_START_END_MARGIN;

  static const unsigned int TIMER_REPEATING_KEYWORD_ATTRIBS
    = PRIORITY_PVR_IS_REPEATING |
      static_cast<int>(PVR_TIMER_TYPE_SUPPORTS_ENABLE_DISABLE) |
      PVR_TIMER_TYPE_SUPPORTS_RECORD_ONLY_NEW_EPISODES |
      PVR_TIMER_TYPE_SUPPORTS_ANY_CHANNEL |
      PVR_TIMER_TYPE_SUPPORTS_MAX_RECORDINGS;

  static const unsigned int TIMER_ADVANCED_ATTRIBS
    = PVR_TIMER_TYPE_SUPPORTS_CHANNELS |
      PVR_TIMER_TYPE_SUPPORTS_FULLTEXT_EPG_MATCH  |
      PVR_TIMER_TYPE_SUPPORTS_RECORDING_GROUP |
      PVR_TIMER_TYPE_SUPPORTS_START_END_MARGIN;

  /* PVR_Timer.iPriority values and presentation.*/
  static std::vector<kodi::addon::PVRTypeIntValue> priorityValues;
  if (m_settings->m_backendVersion >= NEXTPVR_VERSION_PRIORITY)
  {
    priorityValues.clear();
    // first add the recurring recordings
    tinyxml2::XMLDocument doc;
    if (m_request.DoMethodRequest("recording.recurring.list", doc) == tinyxml2::XML_SUCCESS)
    {
      tinyxml2::XMLNode* recurringsNode = doc.RootElement()->FirstChildElement("recurrings");
      InitializePriorities(recurringsNode);
      priorityValues = {
            {PRIORITY_DEFAULT, kodi::addon::GetLocalizedString(13278)},
            {PRIORITY_IMPORTANT, kodi::addon::GetLocalizedString(30330)},
            {PRIORITY_HIGH, kodi::addon::GetLocalizedString(30331)},
            {PRIORITY_NORMAL, kodi::addon::GetLocalizedString(30332)},
            {PRIORITY_LOW, kodi::addon::GetLocalizedString(30333)},
      };
      for (auto priorities : m_recurringPriorities)
      {
        const std::string displayName = kodi::tools::StringUtils::Format("%d [%s]",
          priorities.first, std::get<RECURRING_PRIORITY_MAP_NAME>(priorities.second).c_str());
        priorityValues.emplace_back(priorities.first, displayName);
        //kodi::Log(ADDON_LOG_DEBUG, "Priorities %d %s", priorities.first, std::get<RECURRING_PRIORITY_MAP_DISPLAY_NAME>(priorities.second).c_str());
      }
      priorityValues.emplace_back(PRIORITY_UNIMPORTANT, kodi::addon::GetLocalizedString(30334));
    }
  }

  /* Timer types definition.*/
    TimerType* t = new TimerType(
    /* One-shot manual (time and channel based) */
        /* Type id. */
        TIMER_ONCE_MANUAL,
        /* Attributes. */
        TIMER_MANUAL_ATTRIBS,
        /* Description. */
        GetTimerDescription(MSG_ONETIME_MANUAL), // "One time (manual)",
        /* Values definitions for attributes. */
        priorityValues, -1, recordingLimitValues, m_defaultLimit, showTypeValues, m_defaultShowType, recordingGroupValues, 0);
    types.emplace_back(*t);
    delete t;

    t = new TimerType(
      /* Type id. */
      TIMER_ONCE_EPG,
      /* Attributes. */
      TIMER_EPG_ATTRIBS,
      /* Description. */
      GetTimerDescription(MSG_ONETIME_GUIDE), // "One time (guide)",
      /* Values definitions for attributes. */
      priorityValues, -1, recordingLimitValues, m_defaultLimit, showTypeValues, m_defaultShowType, recordingGroupValues, 0);
    types.emplace_back(*t);
    delete t;

    /* Repeating manual (time and channel based) Parent */
    t = new TimerType(
      /* Type id. */
      TIMER_REPEATING_MANUAL,
      /* Attributes. */
      TIMER_MANUAL_ATTRIBS | TIMER_REPEATING_MANUAL_ATTRIBS,
      /* Description. */
      GetTimerDescription(MSG_REPEATING_MANUAL), // "Repeating (manual)"
      /* Values definitions for attributes. */
      priorityValues, -1, recordingLimitValues, m_defaultLimit, showTypeValues, m_defaultShowType, recordingGroupValues, 0);
    types.emplace_back(*t);
    delete t;

    /* Repeating epg based Parent*/
    t = new TimerType(
        /* Type id. */
      TIMER_REPEATING_EPG,
        /* Attributes. */
      m_settings->m_backendVersion >= NEXTPVR_VERSION_PRIORITY ? PVR_TIMER_TYPE_SUPPORTS_PRIORITY | TIMER_EPG_ATTRIBS | TIMER_REPEATING_EPG_ATTRIBS
        : TIMER_EPG_ATTRIBS | TIMER_REPEATING_EPG_ATTRIBS,
        /* Description. */
      GetTimerDescription(MSG_REPEATING_GUIDE), // "Repeating (guide)"
      /* Values definitions for attributes. */
      priorityValues, -1,recordingLimitValues, m_defaultLimit, showTypeValues, m_defaultShowType, recordingGroupValues, 0);
    types.emplace_back(*t);
    delete t;

    /* Read-only one-shot for timers generated by timerec */
    t = new TimerType(
      /* Type id. */
      TIMER_ONCE_MANUAL_CHILD,
      /* Attributes. */
      TIMER_MANUAL_ATTRIBS | TIMER_CHILD_ATTRIBUTES,
      /* Description. */
      GetTimerDescription(MSG_REPEATING_CHILD), // "Created by Repeating Timer"
      /* Values definitions for attributes. */
      priorityValues, -1, recordingLimitValues, m_defaultLimit, showTypeValues, m_defaultShowType, recordingGroupValues, 0);
    types.emplace_back(*t);
    delete t;

    /* Read-only one-shot for timers generated by autorec */
    t = new TimerType(
      /* Type id. */
      TIMER_ONCE_EPG_CHILD,
      /* Attributes. */
      TIMER_EPG_ATTRIBS | TIMER_CHILD_ATTRIBUTES,
      /* Description. */
      GetTimerDescription(MSG_REPEATING_CHILD), // "Created by Repeating Timer"
      /* Values definitions for attributes. */
      priorityValues, -1, recordingLimitValues, m_defaultLimit, showTypeValues, m_defaultShowType, recordingGroupValues, 0);
    types.emplace_back(*t);
    delete t;

    /* Repeating epg based Parent*/
    t = new TimerType(
      /* Type id. */
      TIMER_REPEATING_KEYWORD,
      /* Attributes. */
      TIMER_KEYWORD_ATTRIBS | TIMER_REPEATING_KEYWORD_ATTRIBS,
      /* Description. */
      GetTimerDescription(MSG_REPEATING_KEYWORD), // "Repeating (keyword)"
      /* Values definitions for attributes. */
      priorityValues, -1, recordingLimitValues, m_defaultLimit, showTypeValues, m_defaultShowType, recordingGroupValues, 0);
    types.emplace_back(*t);
    delete t;

    t = new TimerType(
      /* Type id. */
      TIMER_REPEATING_ADVANCED,
      /* Attributes. */
      TIMER_ADVANCED_ATTRIBS | TIMER_REPEATING_KEYWORD_ATTRIBS,
      /* Description. */
      GetTimerDescription(MSG_REPEATING_ADVANCED), // "Repeating (advanced)"
      /* Values definitions for attributes. */
      priorityValues, -1, recordingLimitValues, m_defaultLimit, showTypeValues, m_defaultShowType, recordingGroupValues, 0);
    types.emplace_back(*t);
    delete t;

  return PVR_ERROR_NO_ERROR;
}

std::string Timers::GetTimerDescription(int id)
{
  std::string title;
  if (m_settings->m_instancePriority)
    title = kodi::addon::GetLocalizedString(id);
  else
    title = kodi::tools::StringUtils::Format("%s: %s", m_settings->m_instanceName.c_str(), kodi::addon::GetLocalizedString(id).c_str());
  return title;
}

std::string Timers::GetDayString(int dayMask)
{
  std::string days;
  if (dayMask == (PVR_WEEKDAY_SATURDAY | PVR_WEEKDAY_SUNDAY))
  {
    days = "WEEKENDS";
  }
  else if (dayMask == (PVR_WEEKDAY_MONDAY | PVR_WEEKDAY_TUESDAY | PVR_WEEKDAY_WEDNESDAY | PVR_WEEKDAY_THURSDAY | PVR_WEEKDAY_FRIDAY))
  {
    days = "WEEKDAYS";
  }
  else
  {
    if (dayMask & PVR_WEEKDAY_SATURDAY)
      days += "SAT:";
    if (dayMask & PVR_WEEKDAY_SUNDAY)
      days += "SUN:";
    if (dayMask & PVR_WEEKDAY_MONDAY)
      days += "MON:";
    if (dayMask & PVR_WEEKDAY_TUESDAY)
      days += "TUE:";
    if (dayMask & PVR_WEEKDAY_WEDNESDAY)
      days += "WED:";
    if (dayMask & PVR_WEEKDAY_THURSDAY)
      days += "THU:";
    if (dayMask & PVR_WEEKDAY_FRIDAY)
      days += "FRI:";
  }

  return days;
}
PVR_ERROR Timers::AddTimer(const kodi::addon::PVRTimer& timer)
{

  char preventDuplicates[16];
  if (timer.GetPreventDuplicateEpisodes() > 0)
    strcpy(preventDuplicates, "true");
  else
    strcpy(preventDuplicates, "false");
  bool priorityReload = false;

  std::string enabled;
  // NextPVR cannot create new disabled timers
  if (timer.GetState() == PVR_TIMER_STATE_DISABLED)
    if (timer.GetClientIndex() != PVR_TIMER_NO_CLIENT_INDEX)
    {
      enabled = "&enabled=false";
    }
    else
    {
      kodi::Log(ADDON_LOG_ERROR, "Cannot create a new disabled timer");
      return PVR_ERROR_INVALID_PARAMETERS;
    }
  else if (timer.GetState() == PVR_TIMER_STATE_SCHEDULED)
  {
    enabled = "&enabled=true";
  }

  const std::string encodedName = UriEncode(timer.GetTitle());
  const std::string encodedKeyword = UriEncode(timer.GetEPGSearchString());
  const std::string days = GetDayString(timer.GetWeekdays());
  const std::string directory = UriEncode(m_settings->m_recordingDirectories[timer.GetRecordingGroup()]);

  int epgOid = 0;
  if (timer.GetEPGUid() > 0)
  {
    const std::string oidKey = std::to_string(timer.GetEPGUid()) + ":" + std::to_string(timer.GetClientChannelUid());
    epgOid = GetEPGOidForTimer(timer);
    kodi::Log(ADDON_LOG_DEBUG, "TIMER %d %s", epgOid, oidKey.c_str());
  }

  std::string request;

  int marginStart = timer.GetMarginStart();
  int marginEnd = timer.GetMarginEnd();
  if (m_settings->m_ignorePadding && timer.GetClientIndex() == PVR_TIMER_NO_CLIENT_INDEX && marginStart == 0 && marginEnd == 0)
  {
    marginStart = m_settings->m_defaultPrePadding;
    marginEnd = m_settings->m_defaultPostPadding;
  }

  std::string priority;
  int tempPriority = -1;
  int finalPriority = -1;
  if ( timer.GetTimerType() >= TIMER_REPEATING_MIN && m_settings->m_backendVersion >= NEXTPVR_VERSION_PRIORITY)
  {
    m_pvrclient.m_lastRecordingUpdateTime = std::numeric_limits<time_t>::max();
    int selection = timer.GetPriority();
    // PRIORITY_DEFAULT and PRIORITY_UNIMPORTANT on adds are the same
    if (timer.GetClientIndex() == PVR_TIMER_NO_CLIENT_INDEX && selection == PRIORITY_UNIMPORTANT)
    {
      selection = PRIORITY_DEFAULT;
    }

    finalPriority = GetSelectedPriority(selection, timer.GetClientIndex(), tempPriority);
    if (tempPriority > 0)
    {
      priorityReload = true;
      priority = kodi::tools::StringUtils::Format("&priority=%d&reschedule=false", tempPriority);
    }
    else if (finalPriority != PRIORITY_DEFAULT)
    {
      priorityReload = true;
      priority = kodi::tools::StringUtils::Format("&priority=%d&reschedule=false", finalPriority);
    }
  }

  switch (timer.GetTimerType())
  {
  case TIMER_ONCE_MANUAL:
    kodi::Log(ADDON_LOG_DEBUG, "TIMER_ONCE_MANUAL");
    // build one-off recording request
    request = kodi::tools::StringUtils::Format("recording.save&name=%s&recording_id=%d&channel=%d&time_t=%d&duration=%d&pre_padding=%d&post_padding=%d&directory_id=%s",
      encodedName.c_str(),
      timer.GetClientIndex(),
      timer.GetClientChannelUid(),
      static_cast<int>(timer.GetStartTime()),
      static_cast<int>(timer.GetEndTime() - timer.GetStartTime()),
      marginStart,
      marginEnd,
      directory.c_str()
      );
    break;
  case TIMER_ONCE_EPG:
    kodi::Log(ADDON_LOG_DEBUG, "TIMER_ONCE_EPG");
    // build one-off recording request
    request = kodi::tools::StringUtils::Format("recording.save&recording_id=%d&event_id=%d&pre_padding=%d&post_padding=%d&directory_id=%s",
      timer.GetClientIndex(),
      epgOid,
      marginStart,
      marginEnd,
      directory.c_str());
    break;

  case TIMER_ONCE_EPG_CHILD:
    kodi::Log(ADDON_LOG_DEBUG, "TIMER_ONCE_EPG_CHILD");
    // build one-off recording request
    request = kodi::tools::StringUtils::Format("recording.save&recording_id=%d&recurring_id=%d&event_id=%d&pre_padding=%d&post_padding=%d&directory_id=%s",
      timer.GetClientIndex(),
      timer.GetParentClientIndex(),
      epgOid,
      marginStart,
      marginEnd,
      directory.c_str());
    break;

  case TIMER_REPEATING_EPG:
    if (timer.GetClientChannelUid() == PVR_TIMER_ANY_CHANNEL)
    // Fake a manual recording not a specific type in NextPVR
    {
      if (timer.GetEPGSearchString() == TYPE_7_TITLE)
      {
        kodi::Log(ADDON_LOG_DEBUG, "TIMER_REPEATING_EPG ANY CHANNEL - TYPE 7");
        request = kodi::tools::StringUtils::Format("recording.recurring.save&type=7&recurring_id=%d&start_time=%d&end_time=%d&keep=%d&pre_padding=%d&post_padding=%d&day_mask=%s&directory_id=%s%s%s",
          timer.GetClientIndex(),
          static_cast<int>(timer.GetStartTime()),
          static_cast<int>(timer.GetEndTime()),
          timer.GetMaxRecordings(),
          marginStart,
          marginEnd,
          days.c_str(),
          directory.c_str(),
          enabled.c_str(),
          priority.c_str()
          );
      }
      else
      {
        kodi::Log(ADDON_LOG_DEBUG, "TIMER_REPEATING_EPG ANY CHANNEL");
        std::string title = encodedName + "%";
        request = kodi::tools::StringUtils::Format("recording.recurring.save&recurring_id=%d&name=%s&channel_id=%d&start_time=%d&end_time=%d&keep=%d&pre_padding=%d&post_padding=%d&day_mask=%s&directory_id=%s&keyword=%s%s%s",
          timer.GetClientIndex(),
          encodedName.c_str(),
          0,
          static_cast<int>(timer.GetStartTime()),
          static_cast<int>(timer.GetEndTime()),
          timer.GetMaxRecordings(),
          marginStart,
          marginEnd,
          days.c_str(),
          directory.c_str(),
          title.c_str(),
          enabled.c_str(),
          priority.c_str()
          );
      }
    }
    else
    {
      kodi::Log(ADDON_LOG_DEBUG, "TIMER_REPEATING_EPG");
      // build recurring recording request
      request = kodi::tools::StringUtils::Format("recording.recurring.save&recurring_id=%d&channel_id=%d&event_id=%d&keep=%d&pre_padding=%d&post_padding=%d&day_mask=%s&directory_id=%s&only_new=%s%s%s",
        timer.GetClientIndex(),
        timer.GetClientChannelUid(),
        epgOid,
        timer.GetMaxRecordings(),
        marginStart,
        marginEnd,
        days.c_str(),
        directory.c_str(),
        preventDuplicates,
        enabled.c_str(),
        priority.c_str()
        );
    }
    break;

  case TIMER_REPEATING_MANUAL:
    kodi::Log(ADDON_LOG_DEBUG, "TIMER_REPEATING_MANUAL");
    // build manual recurring request
    request = kodi::tools::StringUtils::Format("recording.recurring.save&recurring_id=%d&name=%s&channel_id=%d&start_time=%d&end_time=%d&keep=%d&pre_padding=%d&post_padding=%d&day_mask=%s&directory_id=%s%s%s",
      timer.GetClientIndex(),
      encodedName.c_str(),
      timer.GetClientChannelUid(),
      static_cast<int>(timer.GetStartTime()),
      static_cast<int>(timer.GetEndTime()),
      timer.GetMaxRecordings(),
      marginStart,
      marginEnd,
      days.c_str(),
      directory.c_str(),
      enabled.c_str(),
      priority.c_str()
      );
    break;

  case TIMER_REPEATING_KEYWORD:
    kodi::Log(ADDON_LOG_DEBUG, "TIMER_REPEATING_KEYWORD");
    // build manual recurring request
    request = kodi::tools::StringUtils::Format("recording.recurring.save&recurring_id=%d&name=%s&channel_id=%d&start_time=%d&end_time=%d&keep=%d&pre_padding=%d&post_padding=%d&directory_id=%s&keyword=%s&only_new=%s%s%s",
      timer.GetClientIndex(),
      encodedName.c_str(),
      timer.GetClientChannelUid(),
      static_cast<int>(timer.GetStartTime()),
      static_cast<int>(timer.GetEndTime()),
      timer.GetMaxRecordings(),
      marginStart,
      marginEnd,
      directory.c_str(),
      encodedKeyword.c_str(),
      preventDuplicates,
      enabled.c_str(),
      priority.c_str()
      );
    break;

  case TIMER_REPEATING_ADVANCED:
    kodi::Log(ADDON_LOG_DEBUG, "TIMER_REPEATING_ADVANCED");
    // build manual advanced recurring request
    request = kodi::tools::StringUtils::Format("recording.recurring.save&recurring_type=advanced&recurring_id=%d&name=%s&channel_id=%d&start_time=%d&end_time=%d&keep=%d&pre_padding=%d&post_padding=%d&directory_id=%s&advanced=%s&only_new=%s%s%s",
      timer.GetClientIndex(),
      encodedName.c_str(),
      timer.GetClientChannelUid(),
      static_cast<int>(timer.GetStartTime()),
      static_cast<int>(timer.GetEndTime()),
      timer.GetMaxRecordings(),
      marginStart,
      marginEnd,
      directory.c_str(),
      encodedKeyword.c_str(),
      preventDuplicates,
      enabled.c_str(),
      priority.c_str()
      );
    break;
  }

  // send request to NextPVR
  tinyxml2::XMLDocument doc;
  if (m_request.DoMethodRequest(request, doc) == tinyxml2::XML_SUCCESS)
  {
    if (tempPriority > 0)
    {
      tinyxml2::XMLNode* responseNode = doc.RootElement()->FirstChildElement("recurring");
      int returnedId = XMLUtils::GetUIntValue(responseNode, "id");

      std::string buffer;
      XMLUtils::GetString(responseNode, "name", buffer);
      if (timer.GetClientIndex() != PVR_TIMER_NO_CLIENT_INDEX && timer.GetClientIndex() != returnedId) {
        kodi::Log(ADDON_LOG_WARNING, "Unexpected client id %d:%d:", timer.GetClientIndex(), returnedId);
      }
      if (finalPriority == PRIORITY_DEFAULT)
        finalPriority = std::numeric_limits<int>::max();

      BubbleSortPriority(returnedId, tempPriority, finalPriority);
    }
    if (priorityReload)
      m_request.DoMethodRequest("system.reschedule", doc);
    if (!priority.empty() || timer.GetStartTime() <= time(nullptr) && timer.GetEndTime() > time(nullptr))
      m_pvrclient.TriggerRecordingUpdate();
    m_pvrclient.TriggerTimerUpdate();
    return PVR_ERROR_NO_ERROR;
  }

  return PVR_ERROR_FAILED;
}

PVR_ERROR Timers::DeleteTimer(const kodi::addon::PVRTimer& timer, bool forceDelete)
{
  std::string request = "recording.delete&recording_id=" + std::to_string(timer.GetClientIndex());

  // handle recurring recordings
  if (timer.GetTimerType() >= TIMER_REPEATING_MIN && timer.GetTimerType() <= TIMER_REPEATING_MAX)
  {
    request = "recording.recurring.delete&recurring_id=" + std::to_string(timer.GetClientIndex());
  }

  tinyxml2::XMLDocument doc;
  if (m_request.DoMethodRequest(request, doc) == tinyxml2::XML_SUCCESS)
  {
    m_pvrclient.TriggerTimerUpdate();
    if (timer.GetStartTime() <= time(nullptr) && timer.GetEndTime() > time(nullptr))
      m_pvrclient.TriggerRecordingUpdate();
    return PVR_ERROR_NO_ERROR;
  }

  return PVR_ERROR_FAILED;
}

PVR_ERROR Timers::UpdateTimer(const kodi::addon::PVRTimer& timer)
{
  return AddTimer(timer);
}

int Timers::GetEPGOidForTimer(const kodi::addon::PVRTimer& timer)
{
  std::string request = kodi::tools::StringUtils::Format("channel.listings&channel_id=%d&start=%d&end=%d",
    timer.GetClientChannelUid(), timer.GetEPGUid() - 1, timer.GetEPGUid());

  tinyxml2::XMLDocument doc;
  int epgOid = 0;
  if (m_request.DoMethodRequest(request, doc) == tinyxml2::XML_SUCCESS)
  {
    tinyxml2::XMLNode* listingsNode = doc.RootElement()->FirstChildElement("listings");
    for (tinyxml2::XMLNode* pListingNode = listingsNode->FirstChildElement("l"); pListingNode; pListingNode = pListingNode->NextSiblingElement())
    {
      std::string endTime;
      XMLUtils::GetString(pListingNode, "end", endTime);
      endTime.resize(10);
      if (atoi(endTime.c_str()) == timer.GetEPGUid())
      {
        epgOid = XMLUtils::GetIntValue(pListingNode, "id");
        break;
      }
    }
  }
  return epgOid;
}

void Timers::InitializePriorities(tinyxml2::XMLNode* recurringsNode)
{
  int rows = 0;
  // nextPriority ()
  std::map<int, std::tuple<int, int, std::string>> sortedPriorities;
  tinyxml2::XMLNode* recurringNode;
  int maxOid = 0;

  for (recurringNode = recurringsNode->FirstChildElement("recurring"); recurringNode; recurringNode = recurringNode->NextSiblingElement())
  {
    kodi::addon::PVRTimer tag;

    int id = XMLUtils::GetUIntValue(recurringNode, "id");
    if (id > maxOid)
      maxOid = id;
    int nextPriority = XMLUtils::GetUIntValue(recurringNode, "priority");
    if (nextPriority  >= 500000)
      continue;
    std::string name;
    XMLUtils::GetString(recurringNode, "name", name);
    if (sortedPriorities.find(nextPriority) == sortedPriorities.end())
      sortedPriorities[nextPriority] = std::make_tuple(id, rows, name);
    rows++;
  }
  int priorityClass = 0;
  m_recurringPriorities.clear();
  std::fill_n(m_priorityClasses, 5, 0);

  for (auto const& timer : sortedPriorities)
  {
    if ( std::get<1>(timer.second) == 0) {
      priorityClass = PRIORITY_IMPORTANT;
      m_priorityClasses[0] = timer.first;
    }
    else if (std::get<1>(timer.second) == rows - 1 && rows >= 4)
    {
      priorityClass = PRIORITY_UNIMPORTANT;
      if (maxOid > timer.first)
        m_priorityClasses[4] = maxOid;
      else
        m_priorityClasses[4] = timer.first;
    }
    else
    {
      // timer.second is from 0 to rows-1;
      int group = 3 * std::get<1>(timer.second) / rows;
      // 0, 1, 2
      m_priorityClasses[1 + group] = timer.first;
      // -3, -4 -5
      priorityClass = PRIORITY_HIGH - group;
    }
    m_recurringPriorities[timer.first] = std::make_tuple(std::get<0>(timer.second), priorityClass,
      std::get<2>(timer.second));
  }
}

int Timers::GetSelectedPriority(int selection, int oid, int& tempPriority)
{
  // tempPriority will be 0 if the return priority can be saved directly
  // otherwise the it will be saved as an intermediate priority before juggling priorities
  tempPriority = 0;
  int maxPriority = 0;
  int finalPriority = 0;

  for (int i = 4; i >= 0 ; i--)
  {
    if (m_priorityClasses[i] > 0)
    {
      maxPriority = m_priorityClasses[i];
      break;
    }
  }

  if (selection >= 0)
  {
    // numeric selection insert before the selected priority
    if (m_recurringPriorities.find(selection) == m_recurringPriorities.end())
    {
      kodi::Log(ADDON_LOG_DEBUG, "Selected priority not found  %d", selection);
      return selection;
    }
    if (std::get<RECURRING_PRIORITY_MAP_OID>(m_recurringPriorities[selection]) == oid)
    {
      //no change keep priority
      return PRIORITY_DEFAULT;
    }
    if (selection > 1)
    {
      // check for empty priority before the selection.
      finalPriority = selection - 1;
      if (m_recurringPriorities.find(finalPriority) == m_recurringPriorities.end())
      {
        // found a hole try and leave a larger gap if possible
        return SearchGap(finalPriority);
      }
      // no direct hole will move all priorities around selection with bubble sort/
      finalPriority = selection;
    }
    else
    {
      // handle new starting priority when priority 1 exists
      finalPriority = 1;
    }
  }
  else
  {
    // group selection enter at bottom of group
    if (selection == PRIORITY_DEFAULT)
    {
      // should already be blocked
      return PRIORITY_DEFAULT;
    }

    for (auto const& p : m_recurringPriorities)
    {
      if (std::get<RECURRING_PRIORITY_MAP_OID>(p.second) == oid)
      {
        if (std::get<RECURRING_PRIORITY_MAP_CLASS>(p.second) == selection)
        {
          //no change in group
          return PRIORITY_DEFAULT;
        }
        break;
      }
    }
    int priorityClass = -2 - selection;
    if (m_priorityClasses[priorityClass] == 0)
    {
      // not enough to prioritize just append
      return PRIORITY_DEFAULT;
    }
    // get last member the group
    finalPriority = m_priorityClasses[priorityClass];
    if (selection == PRIORITY_IMPORTANT)
    {
      if (m_priorityClasses[0] > 1)
      {
        // holes at the top
        return m_priorityClasses[0] - 1;
      }
      finalPriority = 1;
    }
    else if (selection == PRIORITY_UNIMPORTANT)
    {
      // PRIORITY_UNIMPORTANT is the default on new recordings
      finalPriority = maxPriority;
    }
    else
    {
      // try to insert before the starting priority in the desired group
      for (int i = finalPriority; i > m_priorityClasses[priorityClass - 1]; i--)
      {
        if (m_recurringPriorities.find(i) == m_recurringPriorities.end())
        {
          // try and leave space
          return SearchGap(i);
        }
      }
    }
  }

  // try and find the closest hole in the priorities to reduce swaps
  // potential bubble sort moves required use lowest

  // priority when space is needed if no gaps append or use existing
  tempPriority = PRIORITY_DEFAULT;


  int priorityUp = 0;
  int priorityDown = 0;

  // try and find the closest unused priorities
  for (int i = maxPriority - 1; i > 0; i--)
  {
    if (i == finalPriority)
    {
      // will reverse direction
      continue;
    }
    if (m_recurringPriorities.find(i) == m_recurringPriorities.end())
    {
      // unused found
      if (i < finalPriority)
      {
        // only need first priority above selection
        if (priorityDown > 0 && priorityDown > priorityUp)
          tempPriority = i;
        break;
      }
      else
      {
        priorityDown = 0;
        tempPriority = i;
      }
    }
    else
    {
      // priority used continue looking
      if (i < finalPriority)
        priorityDown++;
      else
        priorityUp++;
    }
  }
  // new priority value or no change
  return SearchGap(finalPriority);
}

//
bool Timers::BubbleSortPriority(int id, int tempPriority, int finalPriority)
{
  int priority = std::numeric_limits<int>::max();
  tinyxml2::XMLDocument doc;
  // priority 1 is highest
  std::string direction = finalPriority < tempPriority ? "higher" : "lower";
  std::string request = kodi::tools::StringUtils::Format("recording.recurring.priority&recurring_id=%d&direction=%s", id, direction.c_str());
  int previous = 0;
  do
  {
    if (m_request.DoMethodRequest(request, doc) == tinyxml2::XML_SUCCESS)
    {
      tinyxml2::XMLNode* responseNode = doc.RootElement(); // ->FirstChildElement("rsp");
      int returnedId = XMLUtils::GetUIntValue(responseNode, "recurring_id");
      priority = XMLUtils::GetUIntValue(responseNode, "priority");
      if (priority == previous)
      {
        kodi::Log(ADDON_LOG_ERROR, "Priority didn't swap %d %d %d", id, priority, finalPriority);
        break;
      }
      if (direction == "higher" && priority < finalPriority)
        break;
      previous = priority;

    }
    else
    {
      return false;
    }
  } while (priority != finalPriority);
  return true;
}

int Timers::SearchGap(int startingPriority)
{
  int updatePriority = startingPriority;
  for (int i = startingPriority - 1; i > 0; i--)
  {
    if (m_recurringPriorities.find(i) == m_recurringPriorities.end())
    {
      updatePriority = i;
    }
    else
    {
      break;
    }
  }
  if (updatePriority < startingPriority)
  {
    updatePriority = updatePriority + std::round((startingPriority - updatePriority) / 2);
  }
  return updatePriority;
}
