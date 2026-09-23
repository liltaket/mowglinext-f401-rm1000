import {expect, test} from "@playwright/test";

const MAX_PROJECTED_ERROR_PX = 3;

test("mower image follows projected map geometry across bearing, pitch, and heading", async ({page}) => {
    await page.route("https://api.mapbox.com/**", (route) => route.fulfill({status: 200, contentType: "application/json", body: "{}"}));
    await page.goto("/tests/e2e/fixtures/map-image-marker.html");
    await page.waitForFunction(() => Boolean(window.mapImageMarkerTest));
    const image = page.locator("img[alt='RM1000 mower test image']");
    await expect(image).toBeVisible();

    const results = await page.evaluate(async () => {
        const testHarness = window.mapImageMarkerTest!;
        const map = testHarness.map;
        const imageElement = document.querySelector<HTMLImageElement>("img[alt='RM1000 mower test image']")!;
        const sourceAnchor = {x: 0.5, y: 0.77};
        const imageLengthM = 0.57 / 0.9;
        const earthRadiusM = 6_378_137;
        const center = map.getCenter();
        const states = [
            {bearing: 0, pitch: 0, heading: 0},
            {bearing: 90, pitch: 0, heading: Math.PI / 2},
            {bearing: 0, pitch: 50, heading: 0},
            {bearing: 90, pitch: 50, heading: Math.PI},
        ];

        const allCorners: Array<{bearing: number; pitch: number; heading: number; error: number}> = [];
        for (const state of states) {
            map.jumpTo({bearing: state.bearing, pitch: state.pitch, zoom: 19});
            await new Promise<void>((resolve) => requestAnimationFrame(() => requestAnimationFrame(() => resolve())));
            testHarness.setHeading(state.heading);
            await new Promise<void>((resolve) => requestAnimationFrame(() => requestAnimationFrame(() => resolve())));
            const renderedBox = imageElement.getBoundingClientRect();
            const source = [
                {x: 0, y: 0}, {x: 1, y: 0}, {x: 1, y: 1}, {x: 0, y: 1},
            ];
            const predicted = source.map(({x, y}) => {
                const eastM = (x - sourceAnchor.x) * imageLengthM;
                const northM = (sourceAnchor.y - y) * imageLengthM;
                const eastHeadingM = eastM * Math.cos(state.heading) - northM * Math.sin(state.heading);
                const northHeadingM = eastM * Math.sin(state.heading) + northM * Math.cos(state.heading);
                const lon = center.lng + (eastHeadingM / (earthRadiusM * Math.cos(center.lat * Math.PI / 180))) * 180 / Math.PI;
                const lat = center.lat + (northHeadingM / earthRadiusM) * 180 / Math.PI;
                const point = map.project([lon, lat]);
                const canvas = map.getCanvas().getBoundingClientRect();
                return {x: canvas.left + point.x, y: canvas.top + point.y};
            });
            const expectedBox = {
                left: Math.min(...predicted.map(({x}) => x)),
                right: Math.max(...predicted.map(({x}) => x)),
                top: Math.min(...predicted.map(({y}) => y)),
                bottom: Math.max(...predicted.map(({y}) => y)),
            };
            allCorners.push(...[
                Math.abs(renderedBox.left - expectedBox.left),
                Math.abs(renderedBox.right - expectedBox.right),
                Math.abs(renderedBox.top - expectedBox.top),
                Math.abs(renderedBox.bottom - expectedBox.bottom),
            ].map((error) => ({
                bearing: state.bearing,
                pitch: state.pitch,
                heading: state.heading,
                error,
            })));
        }

        return allCorners;
    });

    for (const point of results) {
        // Small tolerance covers pixel rounding and the local lat/lon approximation
        // used to create sub-meter reference points.
        expect(point.error, `bearing=${point.bearing}, pitch=${point.pitch}, heading=${point.heading}`)
            .toBeLessThan(MAX_PROJECTED_ERROR_PX);
    }
});
