import {describe, expect, it} from "vitest";
import {getAvailableDockAppearances, resolveDockAppearance, resolveMowerAppearance, shouldDisplayMapImage, shouldDisplayMowerImage} from "./mowerAppearances.ts";

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

    it("keeps the existing dock marker as the default and restricts the Biltema dock to that mower appearance", () => {
        expect(getAvailableDockAppearances("urdf").map(({id}) => id)).toEqual(["marker"]);
        expect(getAvailableDockAppearances("biltema-rm1000").map(({id}) => id)).toEqual([
            "marker", "biltema-rm1000", "biltema-rm1000-clean",
        ]);
        expect(resolveDockAppearance(undefined, "urdf").id).toBe("marker");
        expect(resolveDockAppearance("stale", "biltema-rm1000").id).toBe("marker");
        expect(resolveDockAppearance("biltema-rm1000", "urdf").id).toBe("marker");
        expect(resolveDockAppearance("generic", "urdf").id).toBe("marker");
        expect(resolveDockAppearance("biltema-rm1000", "urdf").id).toBe("marker");
        expect(resolveDockAppearance("biltema-rm1000-clean", "urdf").id).toBe("marker");
        expect(resolveDockAppearance("biltema-rm1000", "biltema-rm1000").image?.src)
            .toBe("/assets/robots/biltema-rm1000/dock.webp");
        expect(resolveDockAppearance("biltema-rm1000-clean", "biltema-rm1000").image?.src)
            .toBe("/assets/robots/biltema-rm1000/dock-clean.webp");
    });

    it("keeps the generic dock marker until the selected image is decoded with a valid pose and heading", () => {
        const image = resolveDockAppearance("biltema-rm1000-clean", "biltema-rm1000").image;
        expect(shouldDisplayMapImage(image, undefined, true, true)).toBe(false);
        expect(shouldDisplayMapImage(image, image?.src, false, true)).toBe(false);
        expect(shouldDisplayMapImage(image, image?.src, true, false)).toBe(false);
        expect(shouldDisplayMapImage(image, image?.src, true, true)).toBe(true);
        expect(shouldDisplayMapImage(undefined, image?.src, true, true)).toBe(false);
    });
});
