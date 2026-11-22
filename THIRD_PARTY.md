# Third-party components

The files `sdk/include/onair_plugin.h` and `sdk/cmake/OnAirPlugin.cmake` come from the [OnAir project](https://github.com/adswill/OnAir). Their authors dedicated them to the public domain under CC0-1.0, as stated in the files. The rest of this repository is licensed under MIT; see `LICENSE`.

The browser map loads Leaflet from unpkg and map tiles from OpenStreetMap while online. Neither library nor tiles are bundled in the `.onairplugin` package. The map displays the OpenStreetMap attribution required for the tiles.
