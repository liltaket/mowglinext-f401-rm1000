import {fireEvent, render, screen, waitFor} from "@testing-library/react";
import {beforeEach, describe, expect, it, vi} from "vitest";
import {MowerImageMarker} from "./MowerImageMarker.tsx";

vi.mock("react-map-gl/mapbox", () => ({
    Marker: ({children, style}: {children: React.ReactNode; style?: React.CSSProperties}) =>
        <div data-testid="map-marker" style={style}>{children}</div>,
    useMap: () => ({current: {
        project: ([longitude, latitude]: [number, number]) => ({x: longitude * 1e9, y: latitude * 1e9}),
        on: vi.fn(),
        off: vi.fn(),
    }}),
}));

describe("MowerImageMarker", () => {
    beforeEach(() => vi.clearAllMocks());

    it("removes a failed image marker and reports failure so the URDF fallback stays visible", async () => {
        const onLoad = vi.fn();
        const onError = vi.fn();
        render(
            <MowerImageMarker
                src="/assets/robots/biltema-rm1000/mower.webp"
                longitude={18}
                latitude={59}
                headingRad={0}
                visibleLengthM={0.6}
                visibleLengthFraction={0.9}
                baseLinkAnchorY={0.77}
                onLoad={onLoad}
                onError={onError}
            />,
        );

        const image = await screen.findByAltText("Biltema RM1000 mower");
        expect(image).toHaveStyle({visibility: "hidden"});
        const marker = screen.getByTestId("map-marker");
        const markerSize = Number.parseFloat(marker.style.width);
        const imageTop = Number.parseFloat((image as HTMLImageElement).style.top);
        expect(imageTop + 0.77 * markerSize).toBeCloseTo(markerSize / 2);
        fireEvent.error(image);

        await waitFor(() => expect(onError).toHaveBeenCalledOnce());
        expect(onLoad).not.toHaveBeenCalled();
        expect(screen.queryByTestId("map-marker")).not.toBeInTheDocument();
    });
});
