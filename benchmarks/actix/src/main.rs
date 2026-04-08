//! bench_actix — Actix-web HTTP server benchmark
//!
//! Endpoints (identical to other benchmarks):
//!   GET  /plaintext      → "Hello, World!"              (text/plain)
//!   GET  /json           → {"message":"Hello, World!"}  (application/json)
//!   POST /echo           → echoes request body          (application/json)
//!   GET  /delay/{ms}     → async timer N ms, {"ok":true}
//!
//! Usage:
//!   ./bench_actix [port]              default port = 8094
//!
//! Env vars:
//!   ACTIX_THREAD_MULTIPLIER=N         worker count (default: nCPU)

use actix_web::{web, App, HttpServer, HttpResponse, Responder};
use std::env;
use std::time::Duration;

async fn plaintext() -> impl Responder {
    HttpResponse::Ok()
        .content_type("text/plain")
        .body("Hello, World!")
}

async fn json() -> impl Responder {
    HttpResponse::Ok()
        .content_type("application/json")
        .body(r#"{"message":"Hello, World!"}"#)
}

async fn echo(body: web::Bytes) -> impl Responder {
    HttpResponse::Ok()
        .content_type("application/json")
        .body(body)
}

async fn delay(path: web::Path<u64>) -> impl Responder {
    let ms = path.into_inner();
    if ms > 0 {
        tokio::time::sleep(Duration::from_millis(ms)).await;
    }
    HttpResponse::Ok()
        .content_type("application/json")
        .body(r#"{"ok":true}"#)
}

#[actix_web::main]
async fn main() -> std::io::Result<()> {
    let args: Vec<String> = env::args().collect();
    let port: u16 = args.get(1)
        .and_then(|s| s.parse().ok())
        .unwrap_or(8094);

    let workers: usize = env::var("ACTIX_THREAD_MULTIPLIER")
        .ok()
        .and_then(|s| s.parse().ok())
        .unwrap_or_else(num_cpus::get);

    eprintln!(
        "[bench] Actix-web server starting on 0.0.0.0:{} with {} worker(s)",
        port, workers
    );

    HttpServer::new(|| {
        App::new()
            .route("/plaintext", web::get().to(plaintext))
            .route("/json", web::get().to(json))
            .route("/echo", web::post().to(echo))
            .route("/delay/{ms}", web::get().to(delay))
    })
    .bind(("0.0.0.0", port))?
    .workers(workers)
    .run()
    .await
}
