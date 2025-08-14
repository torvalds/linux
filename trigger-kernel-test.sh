#!/bin/bash
set -euo pipefail

# Configuration
REPO_OWNER="$(git config --get remote.origin.url | sed -n 's#.*github\.com[/:]\([^/]*\)/\([^/]*\).*#\1#p')"
REPO_NAME="$(git config --get remote.origin.url | sed -n 's#.*github\.com[/:]\([^/]*\)/\([^/]*\).*#\2#p' | sed 's/\.git$//')"
WORKFLOW_NAME="custom-kernel-test.yml"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

log() {
    echo -e "${GREEN}[$(date +'%Y-%m-%d %H:%M:%S')] $1${NC}"
}

warn() {
    echo -e "${YELLOW}[$(date +'%Y-%m-%d %H:%M:%S')] WARNING: $1${NC}"
}

error() {
    echo -e "${RED}[$(date +'%Y-%m-%d %H:%M:%S')] ERROR: $1${NC}"
    exit 1
}

info() {
    echo -e "${BLUE}[$(date +'%Y-%m-%d %H:%M:%S')] INFO: $1${NC}"
}

usage() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Trigger GitHub Actions workflow to test custom kernel"
    echo ""
    echo "OPTIONS:"
    echo "  -b, --build-id BUILD_ID    Git commit hash of the kernel build (default: current HEAD)"
    echo "  -i, --instance-type TYPE   EC2 instance type (default: m7i.xlarge)"
    echo "  -t, --image-type TYPE      Image type ubuntu-22.04 or ubuntu-24.04 (default: ubuntu-24.04)"
    echo "  -w, --wait                 Wait for workflow completion and show logs"
    echo "  -h, --help                 Show this help message"
    echo ""
    echo "Examples:"
    echo "  $0                         # Test kernel at current HEAD"
    echo "  $0 -b abc123def            # Test specific commit"
    echo "  $0 -w                      # Wait for completion"
    echo "  $0 -i m7i.2xlarge -w       # Use larger instance and wait"
}

# Default values
BUILD_ID="$(git rev-parse HEAD)"
INSTANCE_TYPE="m7i.metal-24xl"
IMAGE_TYPE="ubuntu-24.04"
WAIT_FOR_COMPLETION=false

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -b|--build-id)
            BUILD_ID="$2"
            shift 2
            ;;
        -i|--instance-type)
            INSTANCE_TYPE="$2"
            shift 2
            ;;
        -t|--image-type)
            IMAGE_TYPE="$2"
            shift 2
            ;;
        -w|--wait)
            WAIT_FOR_COMPLETION=true
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            error "Unknown option: $1"
            ;;
    esac
done

# Check dependencies
check_dependencies() {
    if ! command -v gh >/dev/null 2>&1; then
        error "GitHub CLI (gh) not found. Please install it: https://cli.github.com/"
    fi
    
    if ! command -v git >/dev/null 2>&1; then
        error "git not found. Please install git."
    fi
    
    # Check if we're in a git repository
    if ! git rev-parse --git-dir >/dev/null 2>&1; then
        error "Not in a git repository."
    fi
    
    # Check GitHub CLI authentication
    if ! gh auth status >/dev/null 2>&1; then
        error "GitHub CLI not authenticated. Run 'gh auth login' first."
    fi
}

# Validate build ID
validate_build_id() {
    if ! git cat-file -e "$BUILD_ID" 2>/dev/null; then
        error "Build ID '$BUILD_ID' is not a valid git commit in this repository."
    fi
    
    # Get the full commit hash
    BUILD_ID="$(git rev-parse "$BUILD_ID")"
    info "Using build ID: $BUILD_ID"
}

# Check if kernel artifacts exist in S3
check_kernel_artifacts() {
    log "Checking if kernel artifacts exist for build ID: $BUILD_ID"
    
    S3_BUCKET="unvariance-kernel-dev"
    S3_REGION="us-east-2"
    KERNEL_KEY="kernels/${BUILD_ID}/bzImage"
    METADATA_KEY="kernels/${BUILD_ID}/metadata.json"
    
    # Check if AWS CLI is available
    if command -v aws >/dev/null 2>&1; then
        # Check kernel image
        if aws s3 ls "s3://${S3_BUCKET}/${KERNEL_KEY}" --region "$S3_REGION" >/dev/null 2>&1; then
            log "✅ Kernel image found in S3"
        else
            warn "Kernel image not found in S3. You may need to build and upload first:"
            warn "  ./build-and-upload.sh"
        fi
        
        # Check metadata and get initrd key from it
        if aws s3 ls "s3://${S3_BUCKET}/${METADATA_KEY}" --region "$S3_REGION" >/dev/null 2>&1; then
            log "✅ Metadata found in S3"
            
            # Download metadata to get initrd path
            local temp_metadata="/tmp/kernel-metadata-${BUILD_ID}.json"
            if aws s3 cp "s3://${S3_BUCKET}/${METADATA_KEY}" "$temp_metadata" --region "$S3_REGION" >/dev/null 2>&1; then
                # Extract initrd path from metadata
                if command -v jq >/dev/null 2>&1; then
                    INITRD_KEY=$(jq -r '.initrd_path' "$temp_metadata" 2>/dev/null)
                else
                    # Fallback to grep/sed if jq is not available
                    INITRD_KEY=$(grep -o '"initrd_path"[[:space:]]*:[[:space:]]*"[^"]*"' "$temp_metadata" | sed 's/.*"initrd_path"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/')
                fi
                
                if [[ -n "$INITRD_KEY" && "$INITRD_KEY" != "null" ]]; then
                    # Check if initrd exists at the path specified in metadata
                    if aws s3 ls "s3://${S3_BUCKET}/${INITRD_KEY}" --region "$S3_REGION" >/dev/null 2>&1; then
                        log "✅ Initrd image found in S3: ${INITRD_KEY}"
                    else
                        warn "Initrd image not found at path from metadata: ${INITRD_KEY}"
                    fi
                else
                    warn "Could not extract initrd path from metadata"
                fi
                
                rm -f "$temp_metadata"
            else
                warn "Failed to download metadata file"
            fi
        else
            warn "Metadata not found in S3. You may need to build and upload first:"
            warn "  ./build-and-upload.sh"
        fi
    else
        warn "AWS CLI not found, skipping S3 artifact check"
    fi
}

# Trigger the workflow
trigger_workflow() {
    log "Triggering GitHub Actions workflow..."
    
    if [[ -z "$REPO_OWNER" ]] || [[ -z "$REPO_NAME" ]]; then
        error "Could not determine repository owner/name from git remote"
    fi
    
    info "Repository: $REPO_OWNER/$REPO_NAME"
    info "Workflow: $WORKFLOW_NAME"
    info "Build ID: $BUILD_ID"
    info "Instance Type: $INSTANCE_TYPE"
    info "Image Type: $IMAGE_TYPE"
    
    # Trigger the workflow
    local run_output
    run_output=$(gh workflow run "$WORKFLOW_NAME" \
        --repo "$REPO_OWNER/$REPO_NAME" \
        --field "build-id=$BUILD_ID" \
        --field "instance-type=$INSTANCE_TYPE" \
        --field "image-type=$IMAGE_TYPE" 2>&1) || {
        error "Failed to trigger workflow: $run_output"
    }
    
    log "✅ Workflow triggered successfully!"
    
    # Get the run ID
    sleep 3  # Give GitHub a moment to create the run
    local run_id
    run_id=$(gh run list --repo "$REPO_OWNER/$REPO_NAME" --workflow="$WORKFLOW_NAME" --limit=1 --json databaseId --jq '.[0].databaseId')
    
    if [[ -n "$run_id" ]]; then
        info "Workflow run ID: $run_id"
        info "View workflow: https://github.com/$REPO_OWNER/$REPO_NAME/actions/runs/$run_id"
        
        # Store run ID for potential waiting
        echo "$run_id" > /tmp/last_workflow_run_id
    else
        warn "Could not determine run ID"
    fi
}

# Wait for workflow completion
wait_for_completion() {
    if [[ ! -f /tmp/last_workflow_run_id ]]; then
        error "No workflow run ID found. Cannot wait for completion."
    fi
    
    local run_id
    run_id=$(cat /tmp/last_workflow_run_id)
    
    log "Waiting for workflow completion (run ID: $run_id)..."
    
    # Wait for the workflow to complete
    gh run watch "$run_id" --repo "$REPO_OWNER/$REPO_NAME" || {
        warn "Workflow watch failed or workflow failed"
    }
    
    # Show final status
    local status
    status=$(gh run view "$run_id" --repo "$REPO_OWNER/$REPO_NAME" --json status,conclusion --jq '.status + " - " + .conclusion')
    
    if [[ "$status" == *"success"* ]]; then
        log "✅ Workflow completed successfully!"
    else
        warn "⚠️ Workflow completed with status: $status"
    fi
    
    # Show logs URL
    info "View full logs: https://github.com/$REPO_OWNER/$REPO_NAME/actions/runs/$run_id"
}

# Main execution
main() {
    log "Starting kernel test workflow trigger..."
    
    check_dependencies
    validate_build_id
    check_kernel_artifacts
    trigger_workflow
    
    if [[ "$WAIT_FOR_COMPLETION" == true ]]; then
        wait_for_completion
    else
        info "Workflow triggered. Use -w flag to wait for completion, or check GitHub Actions manually."
    fi
    
    log "Done!"
}

# Run if executed directly
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main "$@"
fi