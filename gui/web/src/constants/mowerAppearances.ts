/** Display-only mower imagery. Keep this separate from ROS hardware presets:
 * similar dimensions or electronics do not establish the external shell brand.
 */
export type MowerAppearanceId = "urdf" | "biltema-rm1000";

export interface MowerAppearance {
    id: MowerAppearanceId;
    labelKey: string;
    mowerImage?: {
        src: string;
        /** Physical length represented by the visible mower in the asset. */
        visibleLengthM: number;
        /** Fraction of the square asset occupied by the mower's visible length. */
        visibleLengthFraction: number;
        /** Position of base_link along the image, measured from its top edge. */
        baseLinkAnchorY: number;
    };
    dockImage?: string;
}

export const MOWER_APPEARANCES: Record<MowerAppearanceId, MowerAppearance> = {
    urdf: {id: "urdf", labelKey: "mapToolbar.mowerAppearanceUrdf"},
    "biltema-rm1000": {
        id: "biltema-rm1000",
        labelKey: "mapToolbar.mowerAppearanceBiltemaRm1000",
        mowerImage: {
            src: "/assets/robots/biltema-rm1000/mower.webp",
            visibleLengthM: 0.60,
            visibleLengthFraction: 0.9,
            // base_link is the rear wheel axis; on this 0.60 m chassis it sits
            // 0.48 m behind the front edge (about 80% down from the nose).
            baseLinkAnchorY: 0.77,
        },
    },
};

/** Unknown/stale GUI values fail closed to the existing URDF drawing. */
export function resolveMowerAppearance(value: unknown): MowerAppearance {
    if (value === "biltema-rm1000") return MOWER_APPEARANCES["biltema-rm1000"];
    return MOWER_APPEARANCES.urdf;
}

export function shouldDisplayMowerImage(
    appearance: MowerAppearance,
    loadedSrc: string | undefined,
    hasPose: boolean,
): boolean {
    return Boolean(appearance.mowerImage && loadedSrc === appearance.mowerImage.src && hasPose);
}

/** ROS yaw is counter-clockwise from east; Mapbox marker rotation is clockwise
 * from north. `rotationAlignment="map"` then keeps that heading with the map
 * when the map bearing changes.
 */
export function rosHeadingToMapboxRotation(headingRad: number): number {
    return 90 - headingRad * (180 / Math.PI);
}
