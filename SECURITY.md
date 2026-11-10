# Security and privacy

AfterLine stores recordings under `~/.local/share/afterline`. Anyone with access to the user's OS account or backups may access them. AfterLine does not encrypt recordings at rest.

Models and Python dependencies are downloaded on first setup. The local Ollama service at `127.0.0.1:11434` is accessed only when summarization is requested. Users should protect this localhost service and should not expose it on public network interfaces.

Do not include sensitive meeting content in public issues or logs. Report security vulnerabilities privately to the repository maintainers via GitHub's vulnerability-reporting mechanism when available.
