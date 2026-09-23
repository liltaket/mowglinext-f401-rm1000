import {useEffect, useState} from "react";
import {Marker, useMap} from "react-map-gl/mapbox";
import {rosHeadingToMapboxRotation} from "../../../constants/mowerAppearances.ts";

const EARTH_RADIUS_M = 6_378_137;
const SCALE_PROBE_M = 0.5;

interface MowerImageMarkerProps {
    src: string;
    longitude: number;
    latitude: number;
    headingRad: number;
    visibleLengthM: number;
    visibleLengthFraction: number;
    baseLinkAnchorY: number;
    onLoad: () => void;
    onError: () => void;
}

/** A map-aligned, physically sized image pinned at base_link. The image stays
 * absent until decoded, leaving MapPage's URDF silhouette visible as fallback.
 */
export function MowerImageMarker({
    src, longitude, latitude, headingRad, visibleLengthM,
    visibleLengthFraction, baseLinkAnchorY, onLoad, onError,
}: MowerImageMarkerProps) {
    const {current: map} = useMap();
    const [assetSizePx, setAssetSizePx] = useState(0);
    const [decoded, setDecoded] = useState(false);
    const [loadFailed, setLoadFailed] = useState(false);

    const handleLoadError = () => {
        setLoadFailed(true);
        onError();
    };

    useEffect(() => {
        if (!map || !Number.isFinite(longitude) || !Number.isFinite(latitude)) return;

        const updateSize = () => {
            const cosLat = Math.max(0.01, Math.cos(latitude * Math.PI / 180));
            const eastProbeLongitude = longitude +
                (SCALE_PROBE_M / (EARTH_RADIUS_M * cosLat)) * (180 / Math.PI);
            const origin = map.project([longitude, latitude]);
            const eastProbe = map.project([eastProbeLongitude, latitude]);
            const pixelsPerMeter = Math.hypot(eastProbe.x - origin.x, eastProbe.y - origin.y) / SCALE_PROBE_M;
            const size = pixelsPerMeter * visibleLengthM / visibleLengthFraction;
            if (Number.isFinite(size) && size > 0) setAssetSizePx(size);
        };

        updateSize();
        map.on("move", updateSize);
        map.on("resize", updateSize);
        return () => {
            map.off("move", updateSize);
            map.off("resize", updateSize);
        };
    }, [map, longitude, latitude, visibleLengthM, visibleLengthFraction]);

    if (assetSizePx === 0 || loadFailed) return null;

    return (
        <Marker
            longitude={longitude}
            latitude={latitude}
            anchor="center"
            rotation={rosHeadingToMapboxRotation(headingRad)}
            rotationAlignment="map"
            style={{width: assetSizePx, height: assetSizePx, pointerEvents: "none"}}
        >
            <img
                src={src}
                alt="Biltema RM1000 mower"
                draggable={false}
                onLoad={(event) => {
                    void event.currentTarget.decode().then(() => {
                        setDecoded(true);
                        onLoad();
                    }, handleLoadError);
                }}
                onError={handleLoadError}
                style={{
                    position: "absolute",
                    left: 0,
                    top: -assetSizePx * baseLinkAnchorY,
                    width: "100%",
                    height: "100%",
                    maxWidth: "none",
                    pointerEvents: "none",
                    userSelect: "none",
                    visibility: decoded ? "visible" : "hidden",
                }}
            />
        </Marker>
    );
}
