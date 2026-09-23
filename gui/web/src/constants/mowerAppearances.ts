/** Display-only mower imagery. Keep this separate from ROS hardware presets:
 * similar dimensions or electronics do not establish the external shell brand.
 */
export type MowerAppearanceId = "urdf" | "biltema-rm1000";

export interface MapImageAppearance {
    src: string;
    altKey: string;
    /** Physical length represented by the source image's long axis. */
    visibleLengthM: number;
    /** Fraction of the source image occupied along its long axis. */
    visibleLengthFraction: number;
    /** Optional calibrated width; omitted for square-scaled mower art. */
    visibleWidthM?: number;
    /** Fraction of the source image occupied along its short axis. */
    visibleWidthFraction?: number;
    /** Pose location in normalized source-image coordinates (0..1). */
    poseAnchor: {x: number; y: number};
}

export interface MowerAppearance {
    id: MowerAppearanceId;
    labelKey: string;
    mowerImage?: MapImageAppearance;
}

export type DockAppearanceId = "marker" | "biltema-rm1000" | "biltema-rm1000-clean";

export interface DockAppearance {
    id: DockAppearanceId;
    labelKey: string;
    image?: MapImageAppearance;
    onlyForMowerAppearance?: MowerAppearanceId;
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

export const DOCK_APPEARANCES: Record<DockAppearanceId, DockAppearance> = {
    marker: {id: "marker", labelKey: "mapToolbar.dockAppearanceMarker"},
    "biltema-rm1000": {
        id: "biltema-rm1000",
        labelKey: "mapToolbar.dockAppearanceBiltemaRm1000",
        image: {
            src: "/assets/robots/biltema-rm1000/dock.webp",
            altKey: "mapToolbar.dockAppearanceBiltemaRm1000Alt",
            visibleLengthM: 0.63,
            visibleLengthFraction: 0.944,
            visibleWidthM: 0.46,
            visibleWidthFraction: 0.667,
            // The image top edge follows dock-local +X; pose sits at x=0.
            poseAnchor: {x: 0.5, y: 0.024},
        },
        onlyForMowerAppearance: "biltema-rm1000",
    },
    "biltema-rm1000-clean": {
        id: "biltema-rm1000-clean",
        labelKey: "mapToolbar.dockAppearanceBiltemaRm1000Clean",
        image: {
            src: "/assets/robots/biltema-rm1000/dock-clean.webp",
            altKey: "mapToolbar.dockAppearanceBiltemaRm1000CleanAlt",
            visibleLengthM: 0.63,
            visibleLengthFraction: 0.951,
            visibleWidthM: 0.46,
            visibleWidthFraction: 0.678,
            // The image top edge follows dock-local +X; pose sits at x=0.
            poseAnchor: {x: 0.5, y: 0.02},
        },
        onlyForMowerAppearance: "biltema-rm1000",
    },
};

export function getAvailableDockAppearances(mowerAppearanceId: MowerAppearanceId): DockAppearance[] {
    return Object.values(DOCK_APPEARANCES)
        .filter((appearance) => !appearance.onlyForMowerAppearance || appearance.onlyForMowerAppearance === mowerAppearanceId);
}

export function resolveDockAppearance(value: unknown, mowerAppearanceId: MowerAppearanceId): DockAppearance {
    if (value !== "biltema-rm1000" && value !== "biltema-rm1000-clean") return DOCK_APPEARANCES.marker;
    const appearance = DOCK_APPEARANCES[value];
    if (appearance.onlyForMowerAppearance && appearance.onlyForMowerAppearance !== mowerAppearanceId) {
        return DOCK_APPEARANCES.marker;
    }
    return appearance;
}

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
    return shouldDisplayMapImage(appearance.mowerImage, loadedSrc, hasPose, hasHeading);
}

export function shouldDisplayMapImage(
    image: MapImageAppearance | undefined,
    loadedSrc: string | undefined,
    hasPose: boolean,
    hasHeading: boolean,
): boolean {
    return Boolean(image && loadedSrc === image.src && hasPose && hasHeading);
}
