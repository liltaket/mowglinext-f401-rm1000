import {describe, expect, it} from "vitest";
import {resolveMowerAppearance, shouldDisplayMowerImage} from "./mowerAppearances.ts";

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
        expect(shouldDisplayMowerImage(appearance, undefined, true, true)).toBe(false);
        expect(shouldDisplayMowerImage(appearance, src, false, true)).toBe(false);
        expect(shouldDisplayMowerImage(appearance, src, true, false)).toBe(false);
        expect(shouldDisplayMowerImage(appearance, "/missing.webp", true, true)).toBe(false);
        expect(shouldDisplayMowerImage(appearance, src, true, true)).toBe(true);
        expect(shouldDisplayMowerImage(resolveMowerAppearance("urdf"), src, true, true)).toBe(false);
    });
});
