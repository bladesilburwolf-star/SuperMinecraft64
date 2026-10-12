const GeoLayout steve_geo[] = {
    GEO_CULLING_RADIUS(1000),
    GEO_OPEN_NODE(),
        GEO_SHADOW(SHADOW_CIRCLE_PLAYER, 0xB4, 100),
        GEO_OPEN_NODE(),
            GEO_DISPLAY_LIST(LAYER_OPAQUE, steve_geo_dl),
        GEO_CLOSE_NODE(),
    GEO_CLOSE_NODE(),
    GEO_END(),
};
