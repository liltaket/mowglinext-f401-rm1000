/** Display-only mower imagery. Keep this separate from ROS hardware presets:
 * similar dimensions or electronics do not establish the external shell brand.
 */
export type MowerAppearanceId = "urdf" | "biltema-rm1000";

export interface MapImageAppearance {
    src: string;
    altKey: string;
    /** Physical length represented by the visible object's long axis. */
    visibleLengthM: number;
    /** Fraction of the square source image occupied along its long axis. */
    visibleLengthFraction: number;
    /** Pose location in normalized source-image coordinates (0..1). */
    poseAnchor: {x: number; y: number};
}

export interface MowerAppearance {
    id: MowerAppearanceId;
    labelKey: string;
    mowerImage?: MapImageAppearance;
    dockImage?: MapImageAppearance;
}

export const MOWER_APPEARANCES: Record<MowerAppearanceId, MowerAppearance> = {
    urdf: {id: "urdf", labelKey: "mapToolbar.mowerAppearanceUrdf"},
    "biltema-rm1000": {
        id: "biltema-rm1000",
        labelKey: "mapToolbar.mowerAppearanceBiltemaRm1000",
        mowerImage: {
            src: "/assets/robots/biltema-rm1000/mower.webp",
            altKey: "mapToolbar.mowerAppearanceBiltemaRm1000Alt",
            visibleLengthM: 0.57,
            visibleLengthFraction: 0.9,
            // The source's long axis runs front-to-back; the image pose point
            // is near the rear axle, not at the visual center of the body.
            poseAnchor: {x: 0.5, y: 0.77},
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
    hasHeading: boolean,
): boolean {
    return Boolean(appearance.mowerImage && loadedSrc === appearance.mowerImage.src && hasPose && hasHeading);
}
