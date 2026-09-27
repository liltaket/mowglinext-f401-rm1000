/**
 * Firmware targets that are safe to infer from the selected mower model.
 *
 * These are fields with a defined board/panel or robot-specific starting
 * profile. RM1000 is source-build-only today and its calibration belongs to
 * this robot, not every mower sharing the chassis.
 */
export type FirmwareSelection = {
    boardType?: string;
    panelType?: string;
    tickPerM?: number;
    wheelBase?: number;
    /** Persisted provenance for each independently editable field. */
    boardTypeOrigin?: FirmwareFieldOrigin;
    panelTypeOrigin?: FirmwareFieldOrigin;
    tickPerMOrigin?: FirmwareFieldOrigin;
    wheelBaseOrigin?: FirmwareFieldOrigin;
    /** Mower model whose automatic defaults were last applied. */
    firmwareSelectionModel?: string;
};

export type FirmwareFieldOrigin = "auto" | "manual" | "legacy";

export type FirmwareModelDefaults = Readonly<Pick<FirmwareSelection, "boardType" | "panelType" | "tickPerM" | "wheelBase">>;

export const FIRMWARE_MODEL_DEFAULTS: Readonly<Record<string, FirmwareModelDefaults>> = {
    YardForce500: {
        // YardForce500 is shared by the Vermut and Mowgli controller variants;
        // the mechanical mower model does not identify the board. The panel
        // is common to both classic variants and is safe to infer.
        panelType: "PANEL_TYPE_YARDFORCE_500_CLASSIC",
    },
    YardForce500B: {
        boardType: "BOARD_YARDFORCE500B",
        panelType: "PANEL_TYPE_YARDFORCE_500B_CLASSIC",
    },
    BiltemaRM1000: {
        boardType: "BOARD_BILTEMA_RM1000_MPU6050_YAW180",
        panelType: "PANEL_TYPE_YARDFORCE_900_ECO",
        tickPerM: 331.6,
        wheelBase: 0.325,
    },
};

export const firmwareDefaultsForModel = (
    mowerModel: unknown,
): FirmwareModelDefaults | undefined => {
    if (typeof mowerModel !== "string") return undefined;
    return FIRMWARE_MODEL_DEFAULTS[mowerModel];
};

/**
 * Apply each model-specific field that follows the selected model. Unrelated
 * settings and fields explicitly changed by the user remain untouched.
 */
export const applyFirmwareModelDefaults = <T extends FirmwareSelection>(
    mowerModel: unknown,
    selection: T,
    manualOverrides: Partial<Record<keyof FirmwareSelection, boolean>> = {},
): T => {
    const defaults = firmwareDefaultsForModel(mowerModel);
    return {
        ...selection,
        // An empty string is deliberate: it overrides Formily's static
        // defaults and leaves an unsupported model visibly unselected.
        ...(manualOverrides.boardType ? {} : {boardType: defaults?.boardType ?? ""}),
        ...(manualOverrides.panelType ? {} : {panelType: defaults?.panelType ?? ""}),
        ...(manualOverrides.tickPerM || defaults?.tickPerM === undefined ? {} : {tickPerM: defaults.tickPerM}),
        ...(manualOverrides.wheelBase || defaults?.wheelBase === undefined ? {} : {wheelBase: defaults.wheelBase}),
    } as T;
};

/**
 * Convert persisted field provenance to the conservative override policy used
 * by the form. Missing/unknown provenance is treated as legacy, so a saved
 * value is never silently replaced with a guessed firmware target.
 */
export const manualOverridesFromProvenance = (
    selection: FirmwareSelection,
): Partial<Record<"boardType" | "panelType" | "tickPerM" | "wheelBase", boolean>> => ({
    boardType: selection.boardTypeOrigin !== "auto",
    panelType: selection.panelTypeOrigin !== "auto",
    tickPerM: selection.tickPerMOrigin !== "auto",
    wheelBase: selection.wheelBaseOrigin !== "auto",
});
