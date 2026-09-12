#include <xf86drm.h>
#include <xf86drmMode.h>
#include "helper.h"

int has_connected_display(int fd)
{
    drmModeRes *res = drmModeGetResources(fd);
    int i;

    if (!res)
        return 0;
    for (i = 0; i < res->count_connectors; i++)
    {
        drmModeConnector *conn =
        drmModeGetConnector(fd, res->connectors[i]);
        if (conn && conn->connection == DRM_MODE_CONNECTED && conn->count_modes > 0)
        {
            drmModeFreeConnector(conn);
            drmModeFreeResources(res);
            return 1;
        }
        if (conn)
            drmModeFreeConnector(conn);
    }
    drmModeFreeResources(res);
    return 0;
}
