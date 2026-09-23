import {useEffect, useRef, useState} from "react";
import {Marker, useMap} from "react-map-gl/mapbox";
import type {MapImageAppearance} from "../../../constants/mowerAppearances.ts";
import {calculateMapImageSizePx, getMapImageAnchorOffsetPx, rosHeadingToMapboxRotation} from "./mapImageMarkerMath.ts";

interface MapImageMarkerProps {
    image: MapImageAppearance;
    alt: string;
    longitude: number;
    latitude: number;
    headingRad: number;
    onLoad: () => void;
    onError: () => void;
}

/** A map-aligned, physically sized image pinned at its pose. The image stays
 * absent until decoded so the map's existing marker can remain as fallback.
 */
export function MapImageMarker(props: MapImageMarkerProps) {
    // A source change gets fresh decode/error/size state. Late decode callbacks
    // from an old source are ignored by the keyed inner component and ref guard.
    return <MapImageMarkerForSource key={props.image.src} {...props} />;
}

function MapImageMarkerForSource({
    image, alt, longitude, latitude, headingRad, onLoad, onError,
}: MapImageMarkerProps) {
    const {current: map} = useMap();
    const [assetSizePx, setAssetSizePx] = useState(0);
    const [decoded, setDecoded] = useState(false);
    const [loadFailed, setLoadFailed] = useState(false);
    const imageRef = useRef<HTMLImageElement>(null);
    const decodeSequence = useRef(0);
    const active = useRef(true);

    useEffect(() => {
        active.current = true;
        return () => {
            active.current = false;
            decodeSequence.current += 1;
        };
    }, []);

    const handleLoadError = (sourceImage: HTMLImageElement, sequence: number) => {
        if (!active.current || imageRef.current !== sourceImage || decodeSequence.current !== sequence) return;
        setLoadFailed(true);
        onError();
    };

    useEffect(() => {
        if (!map || !Number.isFinite(longitude) || !Number.isFinite(latitude)) return;

        const updateSize = () => {
            const size = calculateMapImageSizePx(
                (coordinate) => map.project(coordinate),
                longitude,
                latitude,
                image.visibleLengthM,
                image.visibleLengthFraction,
            );
            setAssetSizePx(size ?? 0);
        };

        updateSize();
        map.on("move", updateSize);
        map.on("resize", updateSize);
        return () => {
            map.off("move", updateSize);
            map.off("resize", updateSize);
        };
    }, [map, longitude, latitude, image.visibleLengthM, image.visibleLengthFraction]);

    if (assetSizePx === 0 || loadFailed) return null;
    const imageOffset = getMapImageAnchorOffsetPx(assetSizePx, image.poseAnchor);

    return (
        <Marker
            longitude={longitude}
            latitude={latitude}
            anchor="center"
            rotation={rosHeadingToMapboxRotation(headingRad)}
            rotationAlignment="map"
            pitchAlignment="map"
            style={{width: assetSizePx, height: assetSizePx, pointerEvents: "none"}}
        >
            <img
                ref={imageRef}
                src={image.src}
                alt={alt}
                draggable={false}
                onLoad={(event) => {
                    const sourceImage = event.currentTarget;
                    const sequence = ++decodeSequence.current;
                    void sourceImage.decode().then(() => {
                        if (!active.current || imageRef.current !== sourceImage || decodeSequence.current !== sequence) return;
                        setDecoded(true);
                        onLoad();
                    }, () => handleLoadError(sourceImage, sequence));
                }}
                onError={(event) => {
                    const sourceImage = event.currentTarget;
                    const sequence = ++decodeSequence.current;
                    handleLoadError(sourceImage, sequence);
                }}
                style={{
                    position: "absolute",
                    left: imageOffset.left,
                    top: imageOffset.top,
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
