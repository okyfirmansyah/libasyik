/**
 * GIN benchmark server
 *
 * Endpoints (mirrors the libasyik benchmark server exactly):
 *   GET  /plaintext      → "Hello, World!"  (text/plain)
 *   GET  /json           → {"message":"Hello, World!"}  (application/json)
 *   POST /echo           → echoes request body back  (application/json)
 *   GET  /delay/:ms      → sleeps N ms (goroutine-based), returns {"ok":true}
 *
 * Usage:
 *   PORT=8081 ./bench_gin          default port = 8081
 *
 * Build:
 *   cd benchmarks/gin && go build -o bench_gin .
 */
package main

import (
	"io"
	"net/http"
	"os"
	"runtime"
	"strconv"
	"time"

	"github.com/gin-gonic/gin"
)

const (
	plaintextBody = "Hello, World!"
	jsonBody      = `{"message":"Hello, World!"}`
	delayBody     = `{"ok":true}`
)

func main() {
	// Use all available CPUs (Go default, stated explicitly for clarity)
	runtime.GOMAXPROCS(runtime.NumCPU())

	// Release mode: disables debug output and router debug prints
	gin.SetMode(gin.ReleaseMode)

	// gin.New() skips the default Logger + Recovery middleware so we measure
	// only routing + handler overhead, matching libasyik's no-log-per-request setup.
	r := gin.New()

	// ── Scenario A: Plaintext ──────────────────────────────────────────────
	r.GET("/plaintext", func(c *gin.Context) {
		c.Data(http.StatusOK, "text/plain", []byte(plaintextBody))
	})

	// ── Scenario B: JSON ────────────────────────────────────────────────────
	r.GET("/json", func(c *gin.Context) {
		c.Data(http.StatusOK, "application/json", []byte(jsonBody))
	})

	// ── Scenario C: POST echo ───────────────────────────────────────────────
	// Reads the full body and echoes it back, mirroring libasyik's req->body access.
	r.POST("/echo", func(c *gin.Context) {
		body, err := io.ReadAll(c.Request.Body)
		if err != nil {
			c.Status(http.StatusInternalServerError)
			return
		}
		c.Data(http.StatusOK, "application/json", body)
	})

	// ── Scenario D: Simulated I/O delay ────────────────────────────────────
	// In Go, time.Sleep suspends only the current goroutine; the scheduler
	// continues running other goroutines. This is the goroutine equivalent of
	// libasyik's fiber-aware asyik::sleep_for.
	r.GET("/delay/:ms", func(c *gin.Context) {
		ms, err := strconv.Atoi(c.Param("ms"))
		if err != nil || ms < 0 {
			ms = 0
		}
		if ms > 10000 {
			ms = 10000
		}
		time.Sleep(time.Duration(ms) * time.Millisecond)
		c.Data(http.StatusOK, "application/json", []byte(delayBody))
	})

	port := os.Getenv("PORT")
	if port == "" {
		port = "8081"
	}

	srv := &http.Server{
		Addr:    "0.0.0.0:" + port,
		Handler: r,
		// Tuned for high-concurrency benchmarks
		ReadTimeout:  30 * time.Second,
		WriteTimeout: 30 * time.Second,
		IdleTimeout:  120 * time.Second,
	}

	println("[bench] GIN bench server ready on 0.0.0.0:" + port)
	println("[bench] Endpoints: GET /plaintext  GET /json  POST /echo  GET /delay/:ms")

	if err := srv.ListenAndServe(); err != nil && err != http.ErrServerClosed {
		panic(err)
	}
}
