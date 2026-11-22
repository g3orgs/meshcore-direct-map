# MeshCore Direct Map – Kurzanleitung

**Testversion 0.3.1 für OnAir 4.4.0 unter Linux x86-64** · Autor: DM1OK

Das Plugin zeichnet Empfangsorte von MeshCore-Gruppenpaketen mit leerem Hop-Pfad auf einer lokalen Browser-Karte. Ein leerer Pfad ist ein Hinweis auf direkten Empfang, aber kein sicherer Nachweis für den ursprünglichen Sender. Zum Installieren brauchst du keinen Compiler und das Beispiel-Plugin „Log to file“ muss nicht laufen.

## Installation

1. Öffne in OnAir **Einstellungen → Plug-ins und Skripte → Plug-ins öffnen…**.
2. Wähle im Reiter **Installiert** die Schaltfläche **Aus Datei installieren…** und öffne `meshcore-direct-map-0.3.1.onairplugin`.
3. Wähle **MeshCore Direct Map** aus, erlaube **Netzwerk** und **Dateien** und setze den Haken bei **Aktiviert**.
4. Trage bei **Channel hash** den gewünschten Kanal-Hash als **genau zwei Hex-Ziffern** ein, zum Beispiel `01` oder `D9`. Der Wert muss zu deinem MeshCore-Kanal passen.

## Erst einmal ohne Empfang testen

Aktiviere **Simulation (fictional packets and GPS)**. Öffne im Browser die Adresse, die im **Plugin-Protokoll** hinter „Map server running“ steht. Standardmäßig ist das `http://127.0.0.1:8765/`. Die Adresse im OnAir-Protokoll ist Text: Kopiere sie in die Adresszeile des Browsers. Die gelbe Kennzeichnung **SIMULATION** zeigt erfundene Punkte an; dafür brauchst du weder Funkempfang noch GPS.

**Simulation zurücksetzen** entfernt alle Testpunkte und hält die Simulation an. **Simulation neu starten** beginnt wieder mit einer leeren Karte. Wenn du die Simulation im Plugin-Manager ausschaltest, verschwinden die Testpunkte und die Karte zeigt echte Messungen.

## Echte Messpunkte sammeln

1. Für Fahrmessungen schließe einen GPS-Empfänger an und stelle sicher, dass `gpsd` läuft. Lass **Statische Position verwenden** ausgeschaltet. Das Plugin liest `gpsd` und benötigt keinen festen USB-Gerätenamen. Mit `gpspipe -w -n 15` kannst du prüfen, ob ein `TPV`-Eintrag mit `mode` 2 oder 3 sowie `lat` und `lon` erscheint. Im Gebäude fehlt oft der Satelliten-Fix.
2. Schalte **Simulation** aus. Wähle in OnAir die Betriebsart **MeshCore** und starte den Empfang.
3. Ein Kartenpunkt entsteht erst, wenn ein passendes Gruppenpaket mit **leerem Hop-Pfad** ankommt und gleichzeitig ein frischer GPS-Fix vorliegt. Die Karte aktualisiert sich automatisch.

Die Karte zeigt **GPS-Fix: ja/nein**, die gewählte Positionsquelle, die Zahl gespeicherter Einträge und die Zahl der Kartenpunkte für die aktuelle Sitzung. Im OnAir-Plugin-Protokoll stehen Änderungen des GPS-Fix und die Zähler nach jedem passenden Paket.

### Ohne USB-GPS an einem festen Standort

Setze im Plugin-Manager den Haken **Statische Position verwenden (GPS ignorieren)**. Unter **Quelle der statischen Position** kannst du **Aus OnAir übernehmen** wählen; trage dafür deinen Empfangsort in OnAir unter **Einstellungen → Standort** ein. Alternativ wähle **Manuell eintragen** und gib im Plugin Breiten- und Längengrad als Dezimalzahlen mit Punkt ein. Der Haken hat Vorrang: Auch wenn GPS angeschlossen ist, verwendet das Plugin dann die feste Position. Entferne den Haken für GPS-Betrieb.

Die OnAir-Übernahme liest unter Linux die gespeicherte Stationsposition aus OnAirs Einstellungsdatei. Ist sie nicht gesetzt, gelöscht oder durch den Streamer-Modus verborgen, meldet das Plugin „OnAir station position unavailable“ und erzeugt keinen Kartenpunkt. Bei manueller Eingabe meldet es ungültige oder fehlende Koordinaten ebenfalls. Auf der Karte und in neuen CSV-Dateien steht, ob die Position von GPS, OnAir oder manueller Eingabe stammt. Eine feste Position zeigt wiederholte Messungen am selben Empfangsort; sie taugt nicht für eine Fahr-Heatmap.

Bei jedem **Neustart des Plugins** beginnt eine neue Messsitzung. Ihre Datei `data/direct-packets-JJJJMMTT-HHMMSS.csv` wird erst beim ersten echten Treffer angelegt. Ein bloßes Stoppen und Starten des MeshCore-Empfangs startet keine neue Datei. Alte Dateien bleiben erhalten. Passende Pakete **ohne gültige Empfängerposition** stehen ebenfalls in der CSV, aber ohne Koordinaten und daher ohne Kartenpunkt. Erfundene Simulationspunkte werden nicht gespeichert.

## Alte Messdatei ansehen

Wähle auf der Browser-Karte unter **Gespeicherte Messdatei auf der Karte anzeigen** eine CSV aus dem `data`-Ordner. Auch die ältere Datei `direct-packets.csv` lässt sich so öffnen. Die Datei wird nur im Browser gelesen und nicht an den Kartenserver hochgeladen. Mit **Zur Live-Karte** wechselst du zurück zur aktuellen Sitzung. Den `data`-Ordner findest du in OnAir über **Seinen Ordner öffnen** beim Plugin. Nachrichtentexte werden in den Messdateien nicht gespeichert.

Zum Ausprobieren ohne GPS kannst du stattdessen `BEISPIEL-FIKTIV.csv` aus dem Plugin-Ordner auswählen. Ihre Punkte sind ausdrücklich erfunden und gehören nicht zu einer echten Messsitzung.

## Wenn die Karte leer bleibt

- Prüfe im Plugin-Protokoll, ob **Map server running** erscheint. Falls der Standardport belegt ist, kann das Plugin einen der nächsten Ports verwenden; die Protokolladresse ist maßgeblich. **Local map port** lässt sich ändern. Danach das Plugin deaktivieren und wieder aktivieren.
- Prüfe, ob **Simulation** für den Test eingeschaltet oder für echte Messungen ausgeschaltet ist.
- Prüfe Kanal-Hash, laufenden MeshCore-Empfang und entweder einen GPS-Fix oder eine gültige feste Position. Weitergeleitete Pakete mit Hops erzeugen keinen Kartenpunkt.
- Für Kartenkacheln und die Kartenbibliothek braucht der Browser eine Internetverbindung. Die Messdaten selbst bleiben auf deinem Rechner; der Kartenanbieter erhält die üblichen Kachelanfragen.

**Hinweis zur Messung:** Mit unverändertem OnAir 4.4.0 färbt die Karte Punkte nach **SNR**, nicht nach Empfangsleistung. SNR eignet sich nur als vorläufiger Qualitätswert. Für vergleichbare Fahrmessungen Antenne, Ausrichtung, Empfängerverstärkung und Funkparameter möglichst konstant halten.
