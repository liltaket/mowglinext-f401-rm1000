import {describe, expect, it} from "vitest";
import {resolveMowerAppearance, rosHeadingToMapboxRotation, shouldDisplayMowerImage} from "./mowerAppearances.ts";

describe("mower appearance registry", () => {
    it("uses the bundled RM1000 image only after an explicit GUI appearance selection", () => {
        expect(resolveMowerAppearance("biltema-rm1000").mowerImage?.src)
            .toBe("/assets/robots/biltema-rm1000/mower.webp");
        expect(resolveMowerAppearance("YardForce500").mowerImage).toBeUndefined();
        expect(resolveMowerAppearance("YardForce500B").mowerImage).toBeUndefined();
    });

    it("falls back to the URDF appearance for unknown values", () => {
        expect(resolveMowerAppearance("custom-shell").id).toBe("urdf");
        expect(resolveMowerAppearance(undefined).id).toBe("urdf");
    });

    it("keeps the URDF silhouette until the selected image is loaded and a pose exists", () => {
        const appearance = resolveMowerAppearance("biltema-rm1000");
        const src = appearance.mowerImage!.src;
        expect(shouldDisplayMowerImage(appearance, undefined, true)).toBe(false);
        expect(shouldDisplayMowerImage(appearance, src, false)).toBe(false);
        expect(shouldDisplayMowerImage(appearance, "/missing.webp", true)).toBe(false);
        expect(shouldDisplayMowerImage(appearance, src, true)).toBe(true);
        expect(shouldDisplayMowerImage(resolveMowerAppearance("urdf"), src, true)).toBe(false);
    });
});

describe("ROS heading conversion for Mapbox markers", () => {
    it("maps east, north, west and south headings to clockwise-from-north rotations", () => {
        expect(rosHeadingToMapboxRotation(0)).toBe(90);
        expect(rosHeadingToMapboxRotation(Math.PI / 2)).toBeCloseTo(0);
        expect(rosHeadingToMapboxRotation(Math.PI)).toBeCloseTo(-90);
        expect(rosHeadingToMapboxRotation(-Math.PI / 2)).toBeCloseTo(180);
    });
});
