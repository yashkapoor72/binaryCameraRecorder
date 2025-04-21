mod protocol;
use actix::Addr;
use actix::AsyncContext;
use actix::Message;
use actix::{Actor, Handler, StreamHandler};
use actix_web::{web, App, Error, HttpRequest, HttpResponse, HttpServer};
use actix_web_actors::ws;
use actix_web_static_files::ResourceFiles;
use anyhow::Context;
use clap::{ArgAction, Parser};
use notify::Watcher;
use once_cell::sync::Lazy;
use serde_json::json;
use single_instance::SingleInstance;
use std::env;
use std::path::Path;
use std::path::PathBuf;
use std::sync::{Arc, Mutex};
use std::time::SystemTime;
use tokio::runtime;
use tracing::instrument;
use tracing::{debug, error, info, warn};

include!(concat!(env!("OUT_DIR"), "/generated.rs"));

pub static RUNTIME: Lazy<runtime::Runtime> = Lazy::new(|| {
    runtime::Builder::new_multi_thread()
        .enable_all()
        .worker_threads(1)
        .build()
        .unwrap()
});

/// Simple web server that watches a directory for GStreamer `*.dot` files in a local path and
/// serves them as a web page allowing you to browse them easily.
#[derive(Parser, Debug)]
#[command(author, version, about, long_about = None)]
struct Args {
    /// Server address
    #[arg(short, long, default_value = "0.0.0.0", action = ArgAction::Set)]
    address: String,

    /// Server port
    #[arg(short, long, default_value_t = 3000, action = ArgAction::Set)]
    port: u16,

    /// Folder to monitor for new .dot files
    #[arg(short, long, action = ArgAction::Set)]
    dotdir: Option<String>,

    /// local .dot file to open, can be used to view  a specific `.dot` file
    #[arg()]
    dot_file: Option<String>,

    /// Opens the served page in the default web browser
    #[arg(short, long)]
    open: bool,

    #[arg(short, long)]
    verbose: bool,
}

struct GstDots {
    gstdot_path: std::path::PathBuf,
    viewer_clients: Arc<Mutex<Vec<Addr<Client>>>>,
    snapshot_clients: Arc<Mutex<Vec<Addr<Client>>>>,
    dot_watcher: Mutex<Option<notify::RecommendedWatcher>>,
    args: Args,
    id: String,
    http_address: String,
    instance: SingleInstance,
    exit_on_socket_close: bool,
}

impl std::fmt::Debug for GstDots {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("GstDots")
            .field("gstdot_path", &self.gstdot_path)
            .field("viewer_clients", &self.viewer_clients)
            .field("snapshot_clients", &self.snapshot_clients)
            .field("dot_watcher", &self.dot_watcher)
            .field("args", &self.args)
            .field("id", &self.id)
            .field("http_address", &self.http_address)
            .field("exit_on_socket_close", &self.exit_on_socket_close)
            .finish()
    }
}

fn get_user_cache_dir() -> PathBuf {
    if let Ok(cache_dir) = env::var("XDG_CACHE_HOME") {
        if !cache_dir.is_empty() {
            return PathBuf::from(cache_dir);
        }
    }

    #[cfg(windows)]
    {
        use windows::Win32::UI::Shell::{
            FOLDERID_InternetCache, SHGetKnownFolderPath, KF_FLAG_DEFAULT,
        };

        unsafe {
            if let Ok(path) = SHGetKnownFolderPath(&FOLDERID_InternetCache, KF_FLAG_DEFAULT, None) {
                if let Ok(path_str) = path.to_string() {
                    return PathBuf::from(path_str);
                }
            }
        }
    }

    let home = if let Ok(home) = env::var("HOME") {
        PathBuf::from(home)
    } else {
        #[cfg(windows)]
        {
            if let Ok(profile) = env::var("USERPROFILE") {
                PathBuf::from(profile)
            } else {
                warn!("Could not find home directory: $HOME is not set, and user database could not be read.");
                PathBuf::from(r"C:\")
            }
        }
        #[cfg(unix)]
        {
            warn!("Could not find home directory: $HOME is not set, and user database could not be read.");
            PathBuf::from("/")
        }
    };

    home.join(".cache")
}

impl GstDots {
    fn new(args: Args) -> Arc<Self> {
        let gstdot_path = args
            .dotdir
            .as_ref()
            .map(std::path::PathBuf::from)
            .unwrap_or_else(|| {
                let mut path = get_user_cache_dir();
                path.push("gstreamer-dots");
                path
            });
        std::fs::create_dir_all(&gstdot_path).expect("Failed to create dot directory");

        let exit_on_socket_close = args.dot_file.as_ref().is_some() || args.open;

        let id = format!("gstdots-{}-{}", args.address, args.port);
        let instance = SingleInstance::new(&id).unwrap();
        info!("Instance {id} is single: {}", instance.is_single());
        let app = Arc::new(Self {
            gstdot_path: gstdot_path.clone(),
            id,
            http_address: format!("http://{}:{}", args.address, args.port),
            args,
            viewer_clients: Arc::new(Mutex::new(Vec::new())),
            snapshot_clients: Arc::new(Mutex::new(Vec::new())),
            dot_watcher: Default::default(),
            exit_on_socket_close,
            instance,
        });
        app.watch_dot_files();

        app
    }

    fn relative_dot_path(&self, dot_path: &Path) -> String {
        dot_path
            .strip_prefix(&self.gstdot_path)
            .unwrap()
            .to_string_lossy()
            .to_string()
    }

    fn dot_path_for_file(&self, path: &std::path::Path) -> std::path::PathBuf {
        let file_name = path.file_name().unwrap();

        self.gstdot_path.join(file_name).with_extension("dot")
    }

    fn modify_time(&self, path: &std::path::Path) -> u128 {
        self.dot_path_for_file(path)
            .metadata()
            .map(|m| m.modified().unwrap_or(std::time::UNIX_EPOCH))
            .unwrap_or(std::time::UNIX_EPOCH)
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap_or_default()
            .as_millis()
    }

    fn collect_dot_files(path: &PathBuf, entries: &mut Vec<(PathBuf, SystemTime)>) {
        if let Ok(read_dir) = std::fs::read_dir(path) {
            for entry in read_dir.flatten() {
                let dot_path = entry.path();
                if dot_path.is_dir() {
                    // Recursively call this function if the path is a directory
                    Self::collect_dot_files(&dot_path, entries);
                } else {
                    // Process only `.dot` files
                    if dot_path.extension().and_then(|e| e.to_str()) == Some("dot") {
                        if let Ok(metadata) = dot_path.metadata() {
                            if let Ok(modified) = metadata.modified() {
                                entries.push((dot_path, modified));
                            }
                        }
                    }
                }
            }
        }
    }

    fn list_dots(&self, client: Addr<Client>) {
        debug!("Listing dot files in {:?}", self.gstdot_path);
        let mut entries: Vec<(PathBuf, SystemTime)> = Vec::new();

        let start_path = PathBuf::from(&self.gstdot_path);
        Self::collect_dot_files(&start_path, &mut entries);

        entries.sort_by(|a, b| a.1.cmp(&b.1));

        for (dot_path, _) in entries {
            let content = match std::fs::read_to_string(&dot_path) {
                Ok(c) => c,
                Err(e) => {
                    error!("===>Error reading file: {dot_path:?}: {e:?}");
                    continue;
                }
            };
            if content.is_empty() {
                error!("===>Empty file: {:?}", dot_path);
                continue;
            }

            let name = self.relative_dot_path(&dot_path);
            debug!("Sending `{name}` to client: {client:?}");
            client.do_send(TextMessage(
                json!({
                    "type": "NewDot",
                    "name": name,
                    "content": content,
                    "creation_time": self.modify_time(&dot_path),
                })
                .to_string(),
            ));
        }
    }

    fn send(&self, msg: String, client_type: ClientType) {
        let clients = if matches!(client_type, ClientType::Snapshot) {
            self.snapshot_clients.lock().unwrap()
        } else {
            self.viewer_clients.lock().unwrap()
        };
        let clients = clients.clone();
        for client in clients.iter() {
            client.do_send(TextMessage(msg.clone()));
        }
    }

    fn watch_dot_files(self: &Arc<Self>) {
        let app_clone = self.clone();
        let mut dot_watcher =
            notify::recommended_watcher(move |event: Result<notify::Event, notify::Error>| {
                match event {
                    Ok(event) => {
                        let wanted = event.paths .iter().any(|p| p.extension().map(|e| e == "dot").unwrap_or(false));
                        if wanted
                        {
                            match event.kind {
                                notify::event::EventKind::Modify(notify::event::ModifyKind::Name(_)) => {
                                    for path in event.paths.iter() {
                                        debug!("File created: {:?}", path);
                                        if path.extension().map(|e| e == "dot").unwrap_or(false) {
                                            let path = path.to_path_buf();
                                            let name = app_clone.relative_dot_path(&path);

                                            debug!("Sending {name}");
                                            match std::fs::read_to_string(&path) {
                                                Ok(content) => {
                                                    app_clone.send(
                                                        json!({
                                                            "type": "NewDot",
                                                            "name": name,
                                                            "content": content,
                                                            "creation_time": app_clone.modify_time(&event.paths[0]),
                                                        })
                                                        .to_string(),
                                                        ClientType::Viewer
                                                    );

                                                },
                                                Err(err) => error!("Could not read file {path:?}: {err:?}"),
                                            }

                                        }
                                    }
                                }
                                notify::event::EventKind::Remove(_) => {
                                    debug!("File removed: {:?}", event.paths);
                                    for path in event.paths.iter() {
                                        debug!("File removed: {:?}", path);
                                        if path.extension().map(|e| e == "dot").unwrap_or(false) {
                                            let path = path.to_path_buf();
                                            let clients = app_clone.viewer_clients.lock().unwrap();
                                            let clients = clients.clone();
                                            let path = path.to_path_buf();
                                            let name = app_clone.relative_dot_path(&path);
                                            let value =
                                                    json!({
                                                        "type": "DotRemoved",
                                                        "name": name,
                                                        "creation_time": app_clone.modify_time(&path),
                                                    });

                                            for client in clients.iter() {
                                                debug!("Sending {value:?} to client: {client:?}");
                                                client.do_send(TextMessage(value.to_string()));
                                            }
                                        }
                                    }
                                }
                                _ => (),
                            }
                        }
                    }
                    Err(err) => error!("watch error: {:?}", err),
                }
            })
            .expect("Could not create dot_watcher");

        info!("Watching dot files in {:?}", self.gstdot_path);
        dot_watcher
            .watch(self.gstdot_path.as_path(), notify::RecursiveMode::Recursive)
            .unwrap();
        *self.dot_watcher.lock().unwrap() = Some(dot_watcher);
    }

    #[instrument(level = "trace")]
    fn add_client(&self, client: Addr<Client>, client_type: ClientType) {
        let mut clients = if matches!(client_type, ClientType::Snapshot) {
            self.snapshot_clients.lock().unwrap()
        } else {
            self.viewer_clients.lock().unwrap()
        };

        info!("{client_type:?} Client added: {:?}", client);
        clients.push(client.clone());
        drop(clients);
        if matches!(client_type, ClientType::Viewer) {
            self.list_dots(client);
        }
    }

    #[instrument(level = "trace")]
    fn remove_client(&self, addr: &Addr<Client>) {
        info!("Client removed: {:?}", addr);
        let mut clients = self.snapshot_clients.lock().unwrap();
        clients.retain(|a| a != addr);

        let mut clients = self.viewer_clients.lock().unwrap();
        clients.retain(|a| a != addr);
        if self.exit_on_socket_close && clients.is_empty() {
            info!("No more clients, exiting");
            std::process::exit(0);
        }
    }

    fn open(&self) -> anyhow::Result<bool> {
        if self.args.dot_file.is_some() || self.args.open {
            let gstdot_path = self
                .args
                .dotdir
                .as_ref()
                .map(std::path::PathBuf::from)
                .unwrap_or_else(|| {
                    let mut path = get_user_cache_dir();
                    path.push("gstreamer-dots");
                    path
                });

            let dot_address = if let Some(dot_file) = self.args.dot_file.as_ref() {
                let dot_path = PathBuf::from(&dot_file);
                let dot_name = dot_path.file_name().unwrap();
                let gstdot_path = gstdot_path.join(dot_name);
                info!("Copying {dot_path:?} to {gstdot_path:?}");
                std::fs::copy(&dot_path, gstdot_path).expect("Failed to copy .dot file");
                format!(
                    "{}?pipeline={}",
                    self.http_address,
                    dot_name.to_str().unwrap()
                )
            } else {
                self.http_address.clone()
            };

            info!("Openning {dot_address}");
            opener::open_browser(dot_address)?;

            return Ok(true);
        }

        // An instance already running but not asked to open anything, let starting the
        // new instance fail
        Ok(false)
    }

    fn open_on_running_instance(&self) -> anyhow::Result<bool> {
        if !self.instance.is_single() {
            info!("Server already running, trying to open dot file");
            self.open()
        } else {
            Ok(false)
        }
    }

    async fn run(self: &Arc<Self>) -> anyhow::Result<()> {
        // Check if another instance is already running
        // If so and user specified a dot file, open it in the running single
        // and exit
        if self.open_on_running_instance()? {
            return Ok(());
        }

        let app_data = web::Data::new(self.clone());
        let address = format!("{}:{}", self.args.address, self.args.port);
        info!("Starting server on http://{}", address);

        if self.args.dot_file.is_some() || self.args.open {
            let self_clone = self.clone();
            RUNTIME.spawn(async move {
                loop {
                    tokio::time::sleep(std::time::Duration::from_secs(1)).await;

                    match self_clone.open() {
                        Ok(true) => break,
                        Err(err) => {
                            error!("Error opening dot file: {:?}", err);

                            break;
                        }
                        _ => (),
                    }
                }
            });
        }

        HttpServer::new(move || {
            let generated = generate();
            App::new()
                .app_data(app_data.clone())
                .route("/ws/", web::get().to(ws_index))
                .route("/snapshot/", web::get().to(snapshot))
                .service(ResourceFiles::new("/", generated))
        })
        .bind(&address)
        .context("Couldn't bind adresss")?
        .run()
        .await
        .context("Couldn't run server")
    }
}

#[derive(Debug, Clone)]
enum ClientType {
    Viewer,
    Snapshot,
}

#[derive(Debug)]
struct Client {
    app: Arc<GstDots>,
    type_: ClientType,
}

#[derive(Message)]
#[rtype(result = "()")] // Indicates that no response is expected
pub struct TextMessage(pub String);

impl Actor for Client {
    type Context = ws::WebsocketContext<Self>;

    fn started(&mut self, ctx: &mut Self::Context) {
        self.app.add_client(ctx.address(), self.type_.clone());
    }

    fn stopping(&mut self, ctx: &mut Self::Context) -> actix::Running {
        self.app.remove_client(&ctx.address());
        actix::Running::Stop
    }
}

impl Handler<TextMessage> for Client {
    type Result = ();

    fn handle(&mut self, msg: TextMessage, ctx: &mut Self::Context) {
        // Send the text message to the WebSocket client
        ctx.text(msg.0);
    }
}

impl StreamHandler<Result<ws::Message, ws::ProtocolError>> for Client {
    fn handle(&mut self, msg: Result<ws::Message, ws::ProtocolError>, _ctx: &mut Self::Context) {
        if let Ok(ws::Message::Text(text)) = msg {
            let msg: protocol::IncomingMessage = match serde_json::from_str(&text) {
                Ok(s) => s,
                Err(e) => {
                    error!("Failed to parse message `{text}`: {}", e);
                    return;
                }
            };

            match msg.type_ {
                protocol::IncomingMessageType::Hello => {
                    debug!("Got Hello message");
                }
                protocol::IncomingMessageType::Snapshot => {
                    self.app.send(
                        json!({
                            "type": "Snapshot"
                        })
                        .to_string(),
                        ClientType::Snapshot,
                    );
                }
            }
        }
    }
}

async fn snapshot(
    req: HttpRequest,
    stream: web::Payload,
    data: web::Data<Arc<GstDots>>,
) -> Result<HttpResponse, Error> {
    let app = data.get_ref().clone();

    ws::start(
        Client {
            app,
            type_: ClientType::Snapshot,
        },
        &req,
        stream,
    )
}

async fn ws_index(
    req: HttpRequest,
    stream: web::Payload,
    data: web::Data<Arc<GstDots>>,
) -> Result<HttpResponse, Error> {
    let app = data.get_ref().clone();

    ws::start(
        Client {
            app,
            type_: ClientType::Viewer,
        },
        &req,
        stream,
    )
}

#[actix_web::main]
async fn main() -> anyhow::Result<()> {
    let args = Args::parse();
    tracing_subscriber::fmt()
        .compact()
        .with_span_events(tracing_subscriber::fmt::format::FmtSpan::CLOSE)
        .with_env_filter(
            tracing_subscriber::filter::EnvFilter::try_from_default_env().unwrap_or_else(|_| {
                tracing_subscriber::filter::EnvFilter::new(format!(
                    "warn{}",
                    if args.verbose {
                        ",gst_dots_viewer=trace"
                    } else {
                        ",gst_dots_viewer=info"
                    }
                ))
            }),
        )
        .init();

    let gstdots = GstDots::new(args);
    gstdots.run().await
}
