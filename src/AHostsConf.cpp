#include "stdafx.h"
#include "AHostsConf.h"

int AHostsConf::Load()
{
	FILE *fp = fopen("hosts.ext", "r");
	if (!fp)
		PELOG_LOG((PLV_ERROR, "Warning: unable to open config file hosts.ext.\n"));
	else
	{

	}
	return 0;
}