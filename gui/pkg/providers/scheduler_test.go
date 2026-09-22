package providers

import (
	"encoding/json"
	"testing"
	"time"

	"github.com/mowglinext/mowglinext/pkg/msgs/mowgli"
	"github.com/mowglinext/mowglinext/pkg/types"
	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

// storeSchedule marshals a schedule and stores it in the mock DB under the
// canonical key used by both the API and the scheduler.
func storeSchedule(t *testing.T, db *types.MockDBProvider, s schedule) {
	t.Helper()
	data, err := json.Marshal(s)
	require.NoError(t, err)
	require.NoError(t, db.Set(schedulerKeyPrefix+s.ID, data))
}

func acceptStart(_ string, _ any, res any) {
	if out, ok := res.(*mowgli.HighLevelControlRes); ok {
		out.Success = true
	}
}

// buildScheduler creates a SchedulerProvider backed by mocks without starting
// the background goroutine. Tests set coverageResumeKnown because they assign
// the scheduler state directly instead of receiving the latched ROS topic.
func buildScheduler(ros *types.MockRosProvider, db *types.MockDBProvider) *SchedulerProvider {
	// A healthy behavior_tree_node ACCEPTS the start; tests that need a
	// rejection install their own responder before calling this.
	if ros.ServiceResponder == nil {
		ros.ServiceResponder = acceptStart
	}
	return &SchedulerProvider{
		rosProvider:            ros,
		dbProvider:             db,
		lastHighLevelStateName: "IDLE",
		coverageResumeKnown:    true,
	}
}

// --------------------------------------------------------------------------
// shouldRun
// --------------------------------------------------------------------------

func TestShouldRun_TimeMatch(t *testing.T) {
	now := time.Date(2024, 1, 15, 9, 30, 0, 0, time.Local) // Monday 09:30
	sched := &schedule{
		ID:         "1",
		Time:       "09:30",
		DaysOfWeek: []int{1}, // Monday
		Enabled:    true,
	}
	s := buildScheduler(types.NewMockRosProvider(), types.NewMockDBProvider())
	assert.True(t, s.shouldRun(sched, int(now.Weekday()), now.Format("15:04"), now))
}

func TestShouldRun_TimeMismatch(t *testing.T) {
	now := time.Date(2024, 1, 15, 9, 31, 0, 0, time.Local)
	sched := &schedule{
		ID:         "1",
		Time:       "09:30",
		DaysOfWeek: []int{1},
		Enabled:    true,
	}
	s := buildScheduler(types.NewMockRosProvider(), types.NewMockDBProvider())
	assert.False(t, s.shouldRun(sched, int(now.Weekday()), now.Format("15:04"), now))
}

func TestShouldRun_WrongDay(t *testing.T) {
	now := time.Date(2024, 1, 16, 9, 30, 0, 0, time.Local) // Tuesday
	sched := &schedule{
		ID:         "1",
		Time:       "09:30",
		DaysOfWeek: []int{1}, // Monday only
		Enabled:    true,
	}
	s := buildScheduler(types.NewMockRosProvider(), types.NewMockDBProvider())
	assert.False(t, s.shouldRun(sched, int(now.Weekday()), now.Format("15:04"), now))
}

func TestShouldRun_MultipleDays(t *testing.T) {
	now := time.Date(2024, 1, 17, 8, 0, 0, 0, time.Local) // Wednesday
	sched := &schedule{
		ID:         "1",
		Time:       "08:00",
		DaysOfWeek: []int{1, 3, 5}, // Mon, Wed, Fri
		Enabled:    true,
	}
	s := buildScheduler(types.NewMockRosProvider(), types.NewMockDBProvider())
	assert.True(t, s.shouldRun(sched, int(now.Weekday()), now.Format("15:04"), now))
}

func TestShouldRun_PreventDoubleExecution(t *testing.T) {
	now := time.Date(2024, 1, 15, 9, 30, 0, 0, time.Local)
	recent := now.Add(-30 * time.Second)
	sched := &schedule{
		ID:         "1",
		Time:       "09:30",
		DaysOfWeek: []int{1},
		Enabled:    true,
		LastRun:    &recent,
	}
	s := buildScheduler(types.NewMockRosProvider(), types.NewMockDBProvider())
	assert.False(t, s.shouldRun(sched, int(now.Weekday()), now.Format("15:04"), now))
}

func TestShouldRun_LastRunOldEnough(t *testing.T) {
	now := time.Date(2024, 1, 15, 9, 30, 0, 0, time.Local)
	old := now.Add(-5 * time.Minute)
	sched := &schedule{
		ID:         "1",
		Time:       "09:30",
		DaysOfWeek: []int{1},
		Enabled:    true,
		LastRun:    &old,
	}
	s := buildScheduler(types.NewMockRosProvider(), types.NewMockDBProvider())
	assert.True(t, s.shouldRun(sched, int(now.Weekday()), now.Format("15:04"), now))
}

// --------------------------------------------------------------------------
// safeToStart
// --------------------------------------------------------------------------

func TestSafeToStart_IdleNoEmergency(t *testing.T) {
	s := buildScheduler(types.NewMockRosProvider(), types.NewMockDBProvider())
	s.lastHighLevelState = 1 // IDLE
	s.lastEmergency = false
	assert.True(t, s.safeToStart())
}

func TestSafeToStart_IdleDockedNoEmergency(t *testing.T) {
	s := buildScheduler(types.NewMockRosProvider(), types.NewMockDBProvider())
	s.lastHighLevelState = 1 // IDLE
	s.lastHighLevelStateName = "IDLE_DOCKED"
	s.lastEmergency = false
	assert.True(t, s.safeToStart())
}

func TestSafeToStart_EmergencyActive(t *testing.T) {
	s := buildScheduler(types.NewMockRosProvider(), types.NewMockDBProvider())
	s.lastHighLevelState = 1
	s.lastEmergency = true
	assert.False(t, s.safeToStart())
}

func TestSafeToStart_AlreadyAutonomous(t *testing.T) {
	s := buildScheduler(types.NewMockRosProvider(), types.NewMockDBProvider())
	s.lastHighLevelState = 2 // AUTONOMOUS
	s.lastEmergency = false
	assert.False(t, s.safeToStart())
}

func TestSafeToStart_Recording(t *testing.T) {
	s := buildScheduler(types.NewMockRosProvider(), types.NewMockDBProvider())
	s.lastHighLevelState = 3 // RECORDING
	s.lastEmergency = false
	assert.False(t, s.safeToStart())
}

func TestSafeToStart_NullState(t *testing.T) {
	s := buildScheduler(types.NewMockRosProvider(), types.NewMockDBProvider())
	s.lastHighLevelState = 0 // NULL / transitional
	s.lastEmergency = false
	assert.False(t, s.safeToStart())
}

func TestSafeToStart_ManualMowing(t *testing.T) {
	s := buildScheduler(types.NewMockRosProvider(), types.NewMockDBProvider())
	s.lastHighLevelState = 4 // MANUAL_MOWING
	s.lastEmergency = false
	assert.False(t, s.safeToStart())
}

// These state names are reported as IDLE while the BT is holding a resumable
// mowing session. COMMAND_START in the charging holds is a manual-resume
// request, so a due schedule must never send it.
func TestSafeToStart_ResumableMowingPauses(t *testing.T) {
	for _, name := range []string{
		"CHARGING",
		"CRITICAL_BATTERY_CHARGING",
		"RAIN_WAITING",
	} {
		s := buildScheduler(types.NewMockRosProvider(), types.NewMockDBProvider())
		s.lastHighLevelState = 1 // IDLE
		s.lastHighLevelStateName = name
		s.lastEmergency = false
		assert.False(t, s.safeToStart(), "state_name %s must not be startable", name)
	}
}

// MOWING_COMPLETE still drives the robot home under an active autonomous
// command, so a scheduler must not replace that command with a new start.
func TestSafeToStart_PostMowDockTransitStaysBlocked(t *testing.T) {
	s := buildScheduler(types.NewMockRosProvider(), types.NewMockDBProvider())
	s.lastHighLevelState = 2 // AUTONOMOUS (wheel gate held open)
	s.lastHighLevelStateName = "MOWING_COMPLETE"
	s.lastEmergency = false
	assert.False(t, s.safeToStart())
}

// An emergency also blocks the in-progress post-mow dock transit.
func TestSafeToStart_PostMowDockTransitBlockedByEmergency(t *testing.T) {
	s := buildScheduler(types.NewMockRosProvider(), types.NewMockDBProvider())
	s.lastHighLevelState = 2
	s.lastHighLevelStateName = "MOWING_COMPLETE"
	s.lastEmergency = true
	assert.False(t, s.safeToStart())
}

// The exemption is MOWING_COMPLETE only. Every other AUTONOMOUS transit stays
// blocked: RETURNING_HOME is an explicit operator "go home", and the battery /
// rain docks are the robot protecting itself — starting a mow on a flat battery
// or in the rain is precisely what those states exist to prevent.
func TestSafeToStart_OtherDockTransitsStayBlocked(t *testing.T) {
	for _, name := range []string{
		"RETURNING_HOME",
		"LOW_BATTERY_DOCKING",
		"CRITICAL_BATTERY_DOCKING",
		"RAIN_DETECTED_DOCKING",
		"COVERAGE_FAILED_DOCKING",
		"MOWING",
	} {
		s := buildScheduler(types.NewMockRosProvider(), types.NewMockDBProvider())
		s.lastHighLevelState = 2
		s.lastHighLevelStateName = name
		s.lastEmergency = false
		assert.False(t, s.safeToStart(), "state_name %s must not be startable", name)
	}
}

func TestSafeToStart_RequiresKnownResumeAvailability(t *testing.T) {
	s := buildScheduler(types.NewMockRosProvider(), types.NewMockDBProvider())
	s.lastHighLevelState = 1
	s.lastHighLevelStateName = "IDLE"
	s.coverageResumeKnown = false
	assert.False(t, s.safeToStart())
}

func TestSafeToStart_ResumableIdleSessionStaysBlocked(t *testing.T) {
	s := buildScheduler(types.NewMockRosProvider(), types.NewMockDBProvider())
	s.lastHighLevelState = 1
	s.lastHighLevelStateName = "IDLE"
	s.coverageResumeAvailable = true
	assert.False(t, s.safeToStart())
}

// --------------------------------------------------------------------------
// checkSchedules — integration-style tests using mocks
// --------------------------------------------------------------------------

func TestCheckSchedules_TriggersHighLevelControl(t *testing.T) {
	ros := types.NewMockRosProvider()
	db := types.NewMockDBProvider()

	now := time.Now()
	sched := schedule{
		ID:         "sched-1",
		Time:       now.Format("15:04"),
		DaysOfWeek: []int{int(now.Weekday())},
		Enabled:    true,
	}
	storeSchedule(t, db, sched)

	s := buildScheduler(ros, db)
	s.lastHighLevelState = 1 // IDLE
	s.lastEmergency = false

	s.checkSchedules()

	require.Len(t, ros.ServiceCalls, 1)
	assert.Equal(t, "/behavior_tree_node/high_level_control", ros.ServiceCalls[0].Service)

	req, ok := ros.ServiceCalls[0].Req.(*mowgli.HighLevelControlReq)
	require.True(t, ok, "request should be *mowgli.HighLevelControlReq")
	assert.Equal(t, uint8(1), req.Command, "COMMAND_START must be 1")
}

func TestCheckSchedules_DoesNotStartExistingOrResumableSession(t *testing.T) {
	for _, tc := range []struct {
		name            string
		state           uint8
		stateName       string
		resumeAvailable bool
	}{
		{name: "manual mowing", state: 4, stateName: "MANUAL_MOWING"},
		{name: "charging hold", state: 1, stateName: "CHARGING"},
		{name: "critical charging hold", state: 1, stateName: "CRITICAL_BATTERY_CHARGING"},
		{name: "rain hold", state: 1, stateName: "RAIN_WAITING"},
		{name: "resumable idle", state: 1, stateName: "IDLE", resumeAvailable: true},
	} {
		t.Run(tc.name, func(t *testing.T) {
			ros := types.NewMockRosProvider()
			db := types.NewMockDBProvider()
			now := time.Now()
			sched := schedule{
				ID:         "due",
				Time:       now.Format("15:04"),
				DaysOfWeek: []int{int(now.Weekday())},
				Enabled:    true,
			}
			storeSchedule(t, db, sched)

			s := buildScheduler(ros, db)
			s.lastHighLevelState = tc.state
			s.lastHighLevelStateName = tc.stateName
			s.coverageResumeAvailable = tc.resumeAvailable
			s.checkSchedules()

			assert.Empty(t, ros.ServiceCalls)
			assert.Nil(t, readSchedule(t, db, sched.ID).LastRun)
		})
	}
}

func TestCheckSchedules_DisabledScheduleSkipped(t *testing.T) {
	ros := types.NewMockRosProvider()
	db := types.NewMockDBProvider()

	now := time.Now()
	sched := schedule{
		ID:         "sched-2",
		Time:       now.Format("15:04"),
		DaysOfWeek: []int{int(now.Weekday())},
		Enabled:    false, // disabled
	}
	storeSchedule(t, db, sched)

	s := buildScheduler(ros, db)
	s.lastHighLevelState = 1
	s.checkSchedules()

	assert.Empty(t, ros.ServiceCalls, "disabled schedule must not trigger")
}

func TestCheckSchedules_EmergencyPreventsStart(t *testing.T) {
	ros := types.NewMockRosProvider()
	db := types.NewMockDBProvider()

	now := time.Now()
	sched := schedule{
		ID:         "sched-3",
		Time:       now.Format("15:04"),
		DaysOfWeek: []int{int(now.Weekday())},
		Enabled:    true,
	}
	storeSchedule(t, db, sched)

	s := buildScheduler(ros, db)
	s.lastHighLevelState = 1
	s.lastEmergency = true // emergency active!

	s.checkSchedules()

	assert.Empty(t, ros.ServiceCalls, "emergency must prevent mowing start")
}

func TestCheckSchedules_AlreadyMowingPreventsStart(t *testing.T) {
	ros := types.NewMockRosProvider()
	db := types.NewMockDBProvider()

	now := time.Now()
	sched := schedule{
		ID:         "sched-4",
		Time:       now.Format("15:04"),
		DaysOfWeek: []int{int(now.Weekday())},
		Enabled:    true,
	}
	storeSchedule(t, db, sched)

	s := buildScheduler(ros, db)
	s.lastHighLevelState = 2 // AUTONOMOUS already
	s.lastEmergency = false

	s.checkSchedules()

	assert.Empty(t, ros.ServiceCalls, "already autonomous must prevent double-start")
}

func TestCheckSchedules_PersistsLastRun(t *testing.T) {
	ros := types.NewMockRosProvider()
	db := types.NewMockDBProvider()

	now := time.Now()
	sched := schedule{
		ID:         "sched-5",
		Time:       now.Format("15:04"),
		DaysOfWeek: []int{int(now.Weekday())},
		Enabled:    true,
	}
	storeSchedule(t, db, sched)

	s := buildScheduler(ros, db)
	s.lastHighLevelState = 1
	s.checkSchedules()

	// Re-read from DB and verify LastRun was written
	data, err := db.Get(schedulerKeyPrefix + sched.ID)
	require.NoError(t, err)

	var updated schedule
	require.NoError(t, json.Unmarshal(data, &updated))
	require.NotNil(t, updated.LastRun, "LastRun must be persisted after successful trigger")
}

func TestCheckSchedules_NoDoubleExecutionWithinTwoMinutes(t *testing.T) {
	ros := types.NewMockRosProvider()
	db := types.NewMockDBProvider()

	now := time.Now()
	recent := now.Add(-30 * time.Second)
	sched := schedule{
		ID:         "sched-6",
		Time:       now.Format("15:04"),
		DaysOfWeek: []int{int(now.Weekday())},
		Enabled:    true,
		LastRun:    &recent,
	}
	storeSchedule(t, db, sched)

	s := buildScheduler(ros, db)
	s.lastHighLevelState = 1
	s.checkSchedules()

	assert.Empty(t, ros.ServiceCalls, "schedule run within last 2 minutes must be skipped")
}

func TestCheckSchedules_ServiceErrorDoesNotPersistLastRun(t *testing.T) {
	ros := types.NewMockRosProvider()
	ros.ServiceErr = assert.AnError // simulate rosbridge failure
	db := types.NewMockDBProvider()

	now := time.Now()
	sched := schedule{
		ID:         "sched-7",
		Time:       now.Format("15:04"),
		DaysOfWeek: []int{int(now.Weekday())},
		Enabled:    true,
	}
	storeSchedule(t, db, sched)

	s := buildScheduler(ros, db)
	s.lastHighLevelState = 1
	s.checkSchedules()

	// LastRun must NOT be updated when the service call fails
	data, err := db.Get(schedulerKeyPrefix + sched.ID)
	require.NoError(t, err)
	var updated schedule
	require.NoError(t, json.Unmarshal(data, &updated))
	assert.Nil(t, updated.LastRun, "LastRun must not be persisted after a failed service call")
}

// behavior_tree_node answers START with success=false while it refuses it
// (update maintenance): the round-trip worked, nothing was started (#702).
func TestCheckSchedules_RejectedStartDoesNotPersistLastRun(t *testing.T) {
	ros := types.NewMockRosProvider()
	ros.ServiceResponder = func(_ string, _ any, res any) {
		res.(*mowgli.HighLevelControlRes).Success = false
	}
	db := types.NewMockDBProvider()
	now := time.Now()
	sched := dueSchedule("sched-rejected", now)
	storeSchedule(t, db, sched)

	s := buildScheduler(ros, db)
	s.lastHighLevelState = 1
	s.checkSchedules()

	require.Len(t, ros.ServiceCalls, 1)
	assert.Nil(t, readSchedule(t, db, sched.ID).LastRun, "a rejected START is not a run")
}

// The operator deletes the schedule while its START is in flight: the
// scheduler must not write its pre-call snapshot back (#702).
func TestCheckSchedules_DeleteDuringStartStaysDeleted(t *testing.T) {
	ros := types.NewMockRosProvider()
	db := types.NewMockDBProvider()
	now := time.Now()
	sched := dueSchedule("sched-deleted", now)
	storeSchedule(t, db, sched)
	ros.ServiceResponder = func(service string, req any, res any) {
		require.NoError(t, db.Delete(schedulerKeyPrefix+sched.ID))
		acceptStart(service, req, res)
	}

	s := buildScheduler(ros, db)
	s.lastHighLevelState = 1
	s.checkSchedules()

	_, err := db.Get(schedulerKeyPrefix + sched.ID)
	assert.Error(t, err, "a schedule deleted mid-start must stay deleted")
}

func TestCheckSchedules_EditDuringStartIsPreserved(t *testing.T) {
	ros := types.NewMockRosProvider()
	db := types.NewMockDBProvider()
	now := time.Now()
	sched := dueSchedule("sched-edited", now)
	storeSchedule(t, db, sched)
	edited := sched
	edited.Enabled = false
	edited.Time = "23:59"
	edited.DaysOfWeek = []int{0, 6}
	ros.ServiceResponder = func(service string, req any, res any) {
		storeSchedule(t, db, edited)
		acceptStart(service, req, res)
	}

	s := buildScheduler(ros, db)
	s.lastHighLevelState = 1
	s.checkSchedules()

	got := readSchedule(t, db, sched.ID)
	assert.False(t, got.Enabled, "a schedule disabled mid-start must stay disabled")
	assert.Equal(t, "23:59", got.Time)
	assert.Equal(t, []int{0, 6}, got.DaysOfWeek)
	require.NotNil(t, got.LastRun, "the accepted START is still recorded")
}

// --------------------------------------------------------------------------
// subscribeToStatus — verify that dispatched messages update scheduler state
// --------------------------------------------------------------------------

func TestSubscribeToStatus_UpdatesHighLevelState(t *testing.T) {
	ros := types.NewMockRosProvider()
	db := types.NewMockDBProvider()

	s := &SchedulerProvider{rosProvider: ros, dbProvider: db}
	s.subscribeToStatus()

	// Dispatch a highLevelStatus message with state = 2 (AUTONOMOUS)
	msg, err := json.Marshal(mowgli.HighLevelStatus{State: 2})
	require.NoError(t, err)
	ros.Dispatch("highLevelStatus", msg)

	// Allow the synchronous mock callback to run
	assert.Equal(t, uint8(2), s.lastHighLevelState)
}

func TestSubscribeToStatus_UpdatesEmergencyFlag(t *testing.T) {
	ros := types.NewMockRosProvider()
	db := types.NewMockDBProvider()

	s := &SchedulerProvider{rosProvider: ros, dbProvider: db}
	s.subscribeToStatus()

	msg, err := json.Marshal(mowgli.Emergency{ActiveEmergency: true})
	require.NoError(t, err)
	ros.Dispatch("emergency", msg)

	assert.True(t, s.lastEmergency)
}

func TestSubscribeToStatus_UpdatesCoverageResumeAvailability(t *testing.T) {
	ros := types.NewMockRosProvider()
	db := types.NewMockDBProvider()

	s := &SchedulerProvider{rosProvider: ros, dbProvider: db}
	s.subscribeToStatus()
	ros.Dispatch("coverageResumeAvailable", []byte(`{"data":true}`))

	assert.True(t, s.coverageResumeKnown)
	assert.True(t, s.coverageResumeAvailable)
}
