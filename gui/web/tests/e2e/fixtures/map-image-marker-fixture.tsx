import {useState} from "react";
import {createRoot} from "react-dom/client";
import Map, {useMap} from "react-map-gl/mapbox";
import mapboxgl from "mapbox-gl";
import {DOCK_APPEARANCES, MOWER_APPEARANCES} from "../../../src/constants/mowerAppearances.ts";
import {MapImageMarker} from "../../../src/pages/map/components/MapImageMarker.tsx";
import "mapbox-gl/dist/mapbox-gl.css";

const CENTER: [number, number] = [-122.4194, 37.7749];
const STYLE = {version: 8 as const, sources: {}, layers: []};
mapboxgl.accessToken = "pk.test-no-network-token";
const IMAGE = MOWER_APPEARANCES["biltema-rm1000"].mowerImage!;
const DOCK_IMAGE = DOCK_APPEARANCES["biltema-rm1000"].image!;

declare global {
    interface Window {
        mapImageMarkerTest?: {
            map: mapboxgl.Map;
            setHeading: (headingRad: number) => void;
        };
    }
}

function MarkerFixture() {
    const {current: map} = useMap();
    const [heading, setHeading] = useState(0);
    if (map && !window.mapImageMarkerTest) {
        window.mapImageMarkerTest = {map, setHeading};
    } else if (map && window.mapImageMarkerTest?.setHeading !== setHeading) {
        window.mapImageMarkerTest = {map, setHeading};
    }

    return (
        <>
            <MapImageMarker
                image={DOCK_IMAGE}
                alt="RM1000 dock test image"
                longitude={CENTER[0]}
                latitude={CENTER[1]}
                headingRad={heading}
                onLoad={() => {}}
                onError={() => {}}
            />
            <MapImageMarker
                image={IMAGE}
                alt="RM1000 mower test image"
                longitude={CENTER[0]}
                latitude={CENTER[1]}
                headingRad={heading}
                onLoad={() => {}}
                onError={() => {}}
            />
        </>
    );
}

function App() {
    return (
        <Map
            mapStyle={STYLE}
            initialViewState={{longitude: CENTER[0], latitude: CENTER[1], zoom: 19, bearing: 0, pitch: 0}}
            style={{width: 1200, height: 800}}
            attributionControl={false}
            maxZoom={25}
        >
            <MarkerFixture />
        </Map>
    );
}

createRoot(document.getElementById("root")!).render(<App />);
