#!/usr/bin/env python3
"""Render recorded direct candidates as an interactive map preview."""
import argparse
import csv
import html
import json
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("csv", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    points = []
    with args.csv.open(newline="") as stream:
        for row in csv.DictReader(stream):
            try:
                lat = float(row.get("receiver_lat") or row["gps_lat"])
                lon = float(row.get("receiver_lon") or row["gps_lon"])
                if not (-90 <= lat <= 90 and -180 <= lon <= 180):
                    continue
                level = row.get("level_dbfs", "")
                snr = row.get("snr_db", "")
                if level:
                    metric, value = "dBFS", float(level)
                elif snr:
                    metric, value = "SNR dB", float(snr)
                else:
                    continue
                points.append({"lat": lat, "lon": lon, "metric": metric,
                               "value": value, "time": row["event_unix_s"],
                               "fingerprint": html.escape(row["fingerprint"])})
            except (KeyError, ValueError):
                continue

    payload = json.dumps(points).replace("<", "\\u003c")
    page = """<!doctype html><html lang="de"><meta charset="utf-8">
<title>MeshCore Direct Map</title><meta name="viewport" content="width=device-width,initial-scale=1">
<link rel="stylesheet" href="https://unpkg.com/leaflet@1.9.4/dist/leaflet.css">
<style>html,body,#map{height:100%;margin:0}#legend{position:absolute;z-index:999;top:12px;right:12px;background:white;padding:12px;font:14px sans-serif;box-shadow:0 2px 9px #777}</style>
<div id="map"></div><div id="legend">Direkt empfangene MeshCore-Gruppenpakete<br><span id="count"></span><br>Blau = schwächer · Rot = stärker<br><small>Farbe relativ zum angezeigten Messwert; SNR ist kein Empfangspegel.</small></div>
<script src="https://unpkg.com/leaflet@1.9.4/dist/leaflet.js"></script><script>
const points = __POINTS__;
const map = L.map('map').setView([51, 10], 6);
L.tileLayer('https://tile.openstreetmap.org/{z}/{x}/{y}.png', {attribution:'© OpenStreetMap contributors',maxZoom:19}).addTo(map);
const metric = points.some(p=>p.metric==='dBFS') ? 'dBFS' : 'SNR dB';
const selected = points.filter(p=>p.metric===metric);
document.getElementById('count').textContent = selected.length + ' Punkte · ' + metric;
if (selected.length) {
  const values=selected.map(p=>p.value), lo=Math.min(...values), hi=Math.max(...values);
  const bounds=[];
  for (const p of selected) {
    const t=hi===lo ? .5 : (p.value-lo)/(hi-lo);
    const hue=240*(1-t);
    L.circleMarker([p.lat,p.lon],{radius:10,color:`hsl(${hue} 90% 42%)`,fillColor:`hsl(${hue} 90% 48%)`,fillOpacity:.6,weight:1})
      .bindPopup(`${p.value.toFixed(1)} ${p.metric}<br>${new Date(Number(p.time)*1000).toLocaleString()}<br>${p.fingerprint}`)
      .addTo(map);
    bounds.push([p.lat,p.lon]);
  }
  map.fitBounds(bounds,{padding:[30,30],maxZoom:14});
}
</script></html>"""
    args.output.write_text(page.replace("__POINTS__", payload), encoding="utf-8")
    print(f"{len(points)} receiver positions written to {args.output}")


if __name__ == "__main__":
    main()
