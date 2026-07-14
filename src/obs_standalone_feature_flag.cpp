#include "obs_standalone_feature_flag.hpp"

#include <QByteArray>
#include <QtGlobal>

#ifndef DRAW_STATS_OBS_STANDALONE_CLIENT_DEFAULT_ENABLED
#define DRAW_STATS_OBS_STANDALONE_CLIENT_DEFAULT_ENABLED 1
#endif

namespace draw_stats {

bool obsStandaloneFeatureEnabled()
{
	const QByteArray overrideValue = qgetenv("DRAW_STATS_ENABLE_OBS_STANDALONE_CLIENT").trimmed().toLower();
	if (overrideValue == "1" || overrideValue == "true" || overrideValue == "on" || overrideValue == "yes")
		return true;
	if (overrideValue == "0" || overrideValue == "false" || overrideValue == "off" || overrideValue == "no")
		return false;
	return DRAW_STATS_OBS_STANDALONE_CLIENT_DEFAULT_ENABLED != 0;
}

} // namespace draw_stats
