#!/bin/bash

# cpulimit Management Script for Docker Container
# Supports installation, management, and removal of user CPU limits

set -e

# Configuration
CPULIMIT_DIR="/opt/cpulimit"
SUPERVISOR_CONF_DIR="/etc/supervisor/conf.d"
CPULIMIT_BIN="/usr/local/bin/cpulimit"

# Function to show usage
show_usage() {
    echo "cpulimit Management Script"
    echo ""
    echo "Usage:"
    echo "  $0 install <username> <cpu_limit_percentage>  # Install CPU limit for user"
    echo "  $0 list                                       # List all cpulimit services"
    echo "  $0 start <username>                           # Start cpulimit for user"
    echo "  $0 stop <username>                            # Stop cpulimit for user"
    echo "  $0 restart <username>                         # Restart cpulimit for user"
    echo "  $0 remove <username>                          # Remove cpulimit for user"
    echo "  $0 logs <username>                            # View logs for user"
    echo "  $0 config {edit|show|sync}                    # Manage exclusion configuration"
    echo ""
    echo "Examples:"
    echo "  $0 install webapp 50                          # Limit webapp user to 50% CPU"
    echo "  $0 install webapp 800                         # Limit webapp user to 800% CPU (8 cores)"
    echo "  $0 list                                       # Show all active services"
    echo "  $0 remove webapp                              # Remove limit for webapp user"
    exit 1
}

# Function to log messages
log() {
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] $1"
}

# Management functions
install_cpulimit() {
    if [ $# -ne 2 ]; then
        echo "Usage: $0 install <username> <cpu_limit_percentage>"
        exit 1
    fi

    USERNAME="$1"
    CPU_LIMIT="$2"

    # Validate CPU limit
    if ! [[ "$CPU_LIMIT" =~ ^[0-9]+$ ]] || [ "$CPU_LIMIT" -lt 1 ]; then
        echo "Error: CPU limit must be a positive number (1 or higher)"
        exit 1
    fi

    # Check if user exists
    if ! id "$USERNAME" &>/dev/null; then
        echo "Error: User '$USERNAME' does not exist"
        exit 1
    fi

    log "Starting cpulimit installation for user: $USERNAME with CPU limit: $CPU_LIMIT%"

    # Create cpulimit directories
    log "Creating cpulimit directories..."
    mkdir -p "$CPULIMIT_DIR"
    sudo mkdir -p /etc/cpulimit

    # Install cpulimit binary if not already present
    if [ ! -f "$CPULIMIT_BIN" ]; then
        log "Installing cpulimit binary..."
        if [ -f "cpulimit" ]; then
            cp cpulimit "$CPULIMIT_BIN"
            chmod +x "$CPULIMIT_BIN"
        elif [ -f "src/cpulimit" ]; then
            cp src/cpulimit "$CPULIMIT_BIN"
            chmod +x "$CPULIMIT_BIN"
        else
            log "Building cpulimit from source..."
            make clean && make
            cp cpulimit "$CPULIMIT_BIN"
            chmod +x "$CPULIMIT_BIN"
        fi
    else
        log "cpulimit binary already installed at $CPULIMIT_BIN"
    fi

    # Install system-level exclusion config file
    if [ ! -f "/etc/cpulimit/exclude.conf" ]; then
        if [ -f "exclude.conf" ]; then
            log "Installing exclusion configuration..."
            sudo cp exclude.conf /etc/cpulimit/exclude.conf
        else
            log "Error: exclude.conf not found in current directory"
            log "Please ensure exclude.conf is present in the project directory"
            exit 1
        fi
    else
        log "Exclusion configuration already exists at /etc/cpulimit/exclude.conf"
    fi

    # Create supervisor configuration
    log "Creating supervisor configuration..."
    cat > "$SUPERVISOR_CONF_DIR/cpulimit-$USERNAME.conf" << EOF
[program:cpulimit-$USERNAME]
command=$CPULIMIT_BIN -u $USERNAME -l $CPU_LIMIT
directory=$CPULIMIT_DIR
autostart=true
autorestart=true
startretries=3
user=root
redirect_stderr=true
stdout_logfile=/var/log/cpulimit-$USERNAME.log
stdout_logfile_maxbytes=10MB
stdout_logfile_backups=5
killasgroup=true
stopasgroup=true
priority=999
EOF

    # Create log directory if it doesn't exist
    mkdir -p /var/log

    # Update supervisor configuration
    log "Updating supervisor configuration..."
    sudo supervisorctl reread
    
    # Check if service already exists before update
    SERVICE_EXISTS=$(sudo supervisorctl status "cpulimit-$USERNAME" 2>/dev/null | grep -c "cpulimit-$USERNAME" || echo "0")
    
    sudo supervisorctl update
    
    # Wait a moment for supervisor to process
    sleep 2

    # Check final service status
    if sudo supervisorctl status "cpulimit-$USERNAME" | grep -q "RUNNING"; then
        log "✓ cpulimit service for user $USERNAME is running successfully"
    else
        log "Service not running, attempting to start..."
        sudo supervisorctl start "cpulimit-$USERNAME"
        sleep 2
        if sudo supervisorctl status "cpulimit-$USERNAME" | grep -q "RUNNING"; then
            log "✓ cpulimit service for user $USERNAME is now running successfully"
        else
            log "✗ Failed to start cpulimit service for user $USERNAME"
            sudo supervisorctl status "cpulimit-$USERNAME"
            exit 1
        fi
    fi

    log "Installation completed successfully!"
    log ""
    log "Management commands:"
    log "  $0 list                              # List all cpulimit services"
    log "  $0 start $USERNAME                   # Start cpulimit for $USERNAME"
    log "  $0 stop $USERNAME                    # Stop cpulimit for $USERNAME"
    log "  $0 restart $USERNAME                 # Restart cpulimit for $USERNAME"
    log "  $0 remove $USERNAME                  # Remove cpulimit for $USERNAME"
    log "  $0 logs $USERNAME                    # View logs for $USERNAME"
    log ""
    log "Current status:"
    sudo supervisorctl status "cpulimit-$USERNAME"
}

list_services() {
    echo "Active cpulimit services:"
    sudo supervisorctl status | grep "cpulimit-" || echo "No cpulimit services found"
}

start_service() {
    if [ -z "$1" ]; then
        echo "Usage: $0 start <username>"
        exit 1
    fi
    sudo supervisorctl start "cpulimit-$1"
}

stop_service() {
    if [ -z "$1" ]; then
        echo "Usage: $0 stop <username>"
        exit 1
    fi
    sudo supervisorctl stop "cpulimit-$1"
}

restart_service() {
    if [ -z "$1" ]; then
        echo "Usage: $0 restart <username>"
        exit 1
    fi
    sudo supervisorctl restart "cpulimit-$1"
}

remove_service() {
    if [ -z "$1" ]; then
        echo "Usage: $0 remove <username>"
        exit 1
    fi
    sudo supervisorctl stop "cpulimit-$1"
    sudo rm -f "/etc/supervisor/conf.d/cpulimit-$1.conf"
    sudo supervisorctl reread
    sudo supervisorctl update
    echo "Removed cpulimit for user $1"
}

show_logs() {
    if [ -z "$1" ]; then
        echo "Usage: $0 logs <username>"
        exit 1
    fi
    tail -f "/var/log/cpulimit-$1.log"
}

# Main command dispatcher
case "$1" in
    install)
        shift
        install_cpulimit "$@"
        ;;
    list)
        list_services
        ;;
    start)
        start_service "$2"
        ;;
    stop)
        stop_service "$2"
        ;;
    restart)
        restart_service "$2"
        ;;
    remove)
        remove_service "$2"
        ;;
    logs)
        show_logs "$2"
        ;;
    config)
        case "$2" in
            edit)
                echo "Opening cpulimit exclusion config for editing..."
                sudo ${EDITOR:-nano} /etc/cpulimit/exclude.conf
                ;;
            show)
                echo "Current cpulimit exclusion configuration:"
                echo "========================================"
                cat /etc/cpulimit/exclude.conf
                ;;
            sync)
                echo "Syncing configuration from project to system..."
                if [ -f "exclude.conf" ]; then
                    if [ ! -f "/etc/cpulimit/exclude.conf" ]; then
                        log "Installing exclusion configuration..."
                        sudo cp exclude.conf /etc/cpulimit/exclude.conf
                        echo "Configuration installed successfully."
                    else
                        log "Updating exclusion configuration..."
                        sudo cp exclude.conf /etc/cpulimit/exclude.conf
                        echo "Configuration updated successfully."
                        echo "Note: You need to restart cpulimit services for changes to take effect."
                        ./install.sh list
                    fi
                else
                    echo "Error: exclude.conf not found in current directory"
                    exit 1
                fi
                ;;
            *)
                echo "Usage: $0 config {edit|show|sync}"
                echo "  edit - Edit the exclusion configuration file"
                echo "  show - Display current configuration"
                echo "  sync - Sync project config to system config"
                exit 1
                ;;
        esac
        ;;
    *)
        show_usage
        ;;
esac