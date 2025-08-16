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
    echo -e "${GREEN}[$(date +'%Y-%m-%d %H:%M:%S')] $1${NC}" >&2
}

warn() {
    echo -e "${YELLOW}[$(date +'%Y-%m-%d %H:%M:%S')] WARNING: $1${NC}" >&2
}

error() {
    echo -e "${RED}[$(date +'%Y-%m-%d %H:%M:%S')] ERROR: $1${NC}" >&2
    exit 1
}

info() {
    echo -e "${BLUE}[$(date +'%Y-%m-%d %H:%M:%S')] INFO: $1${NC}" >&2
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
    echo "  -w, --wait                 Trigger workflow and wait for completion (output JSON)"
    echo "  --wait-existing            Wait for existing latest workflow run (output JSON)"
    echo "  -o, --output FILE          JSON output file (default: kernel-test-results.json)"
    echo "  -h, --help                 Show this help message"
    echo ""
    echo "Examples:"
    echo "  $0                         # Trigger workflow only (default)"
    echo "  $0 -b abc123def            # Test specific commit"
    echo "  $0 -w                      # Trigger and wait, output JSON"
    echo "  $0 --wait-existing         # Wait for existing run, output JSON"
    echo "  $0 -w -o results.json      # Custom output file"
    echo "  $0 -i m7i.2xlarge -w       # Use larger instance and wait"
}

# Default values
BUILD_ID="$(git rev-parse HEAD)"
INSTANCE_TYPE="m7i.metal-24xl"
IMAGE_TYPE="ubuntu-24.04"
MODE="trigger-only"  # trigger-only, wait-existing, trigger-and-wait
OUTPUT_FILE="kernel-test-results.json"

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
            MODE="trigger-and-wait"
            shift
            ;;
        --wait-existing)
            MODE="wait-existing"
            shift
            ;;
        -o|--output)
            OUTPUT_FILE="$2"
            shift 2
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

# Trigger the workflow and return run ID
trigger_workflow_get_id() {
    log "Triggering GitHub Actions workflow..."
    
    if [[ -z "$REPO_OWNER" ]] || [[ -z "$REPO_NAME" ]]; then
        error "Could not determine repository owner/name from git remote"
    fi
    
    info "Repository: $REPO_OWNER/$REPO_NAME"
    info "Workflow: $WORKFLOW_NAME"
    info "Build ID: $BUILD_ID"
    info "Instance Type: $INSTANCE_TYPE"
    info "Image Type: $IMAGE_TYPE"
    
    # Trigger the workflow and capture output
    local run_output
    run_output=$(gh workflow run "$WORKFLOW_NAME" \
        --repo "$REPO_OWNER/$REPO_NAME" \
        --field "build-id=$BUILD_ID" \
        --field "instance-type=$INSTANCE_TYPE" \
        --field "image-type=$IMAGE_TYPE" 2>&1) || {
        error "Failed to trigger workflow: $run_output"
    }
    
    log "✅ Workflow triggered successfully!"
    
    # Extract run URL from output and get run ID
    local run_url
    run_url=$(echo "$run_output" | grep -o 'https://github.com/.*/actions/runs/[0-9]*' | head -1)
    
    local run_id
    if [[ -n "$run_url" ]]; then
        run_id=$(echo "$run_url" | grep -o '[0-9]*$')
    else
        # Fallback to API query if URL extraction fails
        sleep 3  # Give GitHub a moment to create the run
        run_id=$(gh run list --repo "$REPO_OWNER/$REPO_NAME" --workflow="$WORKFLOW_NAME" --limit=1 --json databaseId --jq '.[0].databaseId')
    fi
    
    if [[ -n "$run_id" ]]; then
        info "Workflow run ID: $run_id"
        info "View workflow: https://github.com/$REPO_OWNER/$REPO_NAME/actions/runs/$run_id"
        
        # Store run ID for backward compatibility
        echo "$run_id" > /tmp/last_workflow_run_id
        
        # Return the run ID
        echo "$run_id"
    else
        error "Could not determine run ID"
    fi
}

# Get latest workflow run ID
get_latest_workflow_id() {
    local run_id
    run_id=$(gh run list --repo "$REPO_OWNER/$REPO_NAME" --workflow="$WORKFLOW_NAME" --limit=1 --json databaseId --jq '.[0].databaseId')
    
    if [[ -n "$run_id" ]]; then
        echo "$run_id"
    else
        error "No workflow runs found for workflow: $WORKFLOW_NAME"
    fi
}

# Wait for workflow completion and write JSON results
wait_and_write_json() {
    local run_id="$1"
    local output_path="$2"
    
    if [[ -z "$run_id" ]]; then
        error "No run ID provided to wait function"
    fi
    
    # Wait for the workflow to complete (silently)
    gh run watch "$run_id" --repo "$REPO_OWNER/$REPO_NAME" >/dev/null 2>&1 || true
    
    # Generate and write JSON results
    local json_results
    json_results=$(output_json_results "$run_id")
    
    echo "$json_results" > "$output_path"
    echo "$output_path"  # Return the output path for confirmation
}

# Wait for workflow completion (legacy function for backward compatibility)
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

# Output results as JSON
output_json_results() {
    local run_id="$1"
    
    # Get workflow run information
    local run_info
    run_info=$(gh run view "$run_id" --repo "$REPO_OWNER/$REPO_NAME" --json status,conclusion,startedAt,updatedAt,url,headSha,headBranch,event,workflowName,displayTitle)
    
    # Get job information with steps
    local jobs_info
    jobs_info=$(gh run view "$run_id" --repo "$REPO_OWNER/$REPO_NAME" --json jobs --jq '.jobs')
    
    # Try to extract test results and logs from job outputs
    local test_results="null"
    local all_logs=""
    local step_outputs="[]"
    
    # Get full logs from the workflow run
    local logs_output=""
    local job_logs_json="[]"
    
    # Get logs from individual jobs using gh run view --log --job=<job_id>
    local job_ids job_names
    job_ids=$(echo "$jobs_info" | jq -r '.[].databaseId')
    job_names=$(echo "$jobs_info" | jq -r '.[].name')
    
    # Create array to store individual job logs
    local job_logs_array=()
    
    # Get logs for each job
    local job_count=0
    while IFS= read -r job_id && IFS= read -r job_name <&3; do
        [[ -z "$job_id" ]] && continue
        
        local individual_job_logs
        individual_job_logs=$(gh run view --log --job="$job_id" --repo "$REPO_OWNER/$REPO_NAME" 2>/dev/null || echo "")
        
        if [[ -n "$individual_job_logs" ]]; then
            # Add to combined logs output
            logs_output="${logs_output}\n=== Job: $job_name (ID: $job_id) ===\n${individual_job_logs}\n"
            
            # Escape the logs for JSON using base64 encoding to avoid escaping issues
            local encoded_logs
            encoded_logs=$(echo "$individual_job_logs" | base64 -w 0)
            job_logs_array+=("{\"job_id\": \"$job_id\", \"job_name\": \"$job_name\", \"logs_base64\": \"$encoded_logs\"}")
        else
            # Add placeholder for jobs without logs
            job_logs_array+=("{\"job_id\": \"$job_id\", \"job_name\": \"$job_name\", \"logs_base64\": \"$(echo 'No logs available' | base64 -w 0)\"}")
        fi
        
        ((job_count++))
    done < <(echo "$job_ids") 3< <(echo "$job_names")
    
    # Convert job logs array to JSON
    if [[ ${#job_logs_array[@]} -gt 0 ]]; then
        local job_logs_string
        job_logs_string=$(IFS=,; echo "${job_logs_array[*]}")
        job_logs_json="[$job_logs_string]"
    fi
    
    # Extract step outputs for key test steps
    if [[ -n "$logs_output" ]]; then
        # Store comprehensive logs (last 5000 characters to capture more context) - encoded as base64
        all_logs=$(echo "$logs_output" | tail -c 5000 | base64 -w 0)
        
        # Look for test-specific output patterns
        local test_step_output=""
        
        # Look for PMU test output
        if echo "$logs_output" | grep -q "Run PMU test\|PMU\|perf_event_open"; then
            test_step_output=$(echo "$logs_output" | sed -n '/Run PMU test/,/##\[endgroup\]/p' | tail -50)
        fi
        
        # Look for resctrl test output  
        if echo "$logs_output" | grep -q "resctrl\|Check resctrl support"; then
            local resctrl_output
            resctrl_output=$(echo "$logs_output" | sed -n '/Check resctrl support/,/##\[endgroup\]/p' | tail -20)
            test_step_output="${test_step_output}\n${resctrl_output}"
        fi
        
        # Try to extract structured test results
        if echo "$logs_output" | grep -q -E "(PASS|FAIL|SUCCESS|ERROR|Test.*completed|Tests.*run)"; then
            local passed_count failed_count
            
            # Look for various test result patterns
            passed_count=$(echo "$logs_output" | grep -c -E "(PASS|SUCCESS|✓|Test.*passed)" || echo "0")
            failed_count=$(echo "$logs_output" | grep -c -E "(FAIL|ERROR|✗|Test.*failed)" || echo "0")
            
            if [[ "$passed_count" -gt 0 ]] || [[ "$failed_count" -gt 0 ]]; then
                local escaped_test_output
                escaped_test_output=$(echo "$test_step_output" | sed 's/\\/\\\\/g' | sed 's/"/\\"/g' | sed 's/\t/\\t/g' | sed 's/\r/\\r/g' | tr -d '\000-\010\013-\014\016-\037' | tr '\n' '\\n')
                test_results=$(cat <<EOF
{
  "passed": $passed_count,
  "failed": $failed_count,
  "total": $((passed_count + failed_count)),
  "test_output": "$escaped_test_output"
}
EOF
)
            fi
        fi
        
        # Create step outputs array with key information
        step_outputs=$(echo "$logs_output" | grep -E "^[0-9]{4}-[0-9]{2}-[0-9]{2}T.*Z.*" | tail -10 | jq -R -s 'split("\n")[:-1] | map(select(length > 0))' 2>/dev/null || echo "[]")
    fi
    
    # Combine all information into final JSON
    local final_json
    final_json=$(cat <<EOF
{
  "workflow_run": $run_info,
  "jobs": $jobs_info,
  "job_logs": $job_logs_json,
  "test_results": $test_results,
  "build_id": "$BUILD_ID",
  "instance_type": "$INSTANCE_TYPE",
  "image_type": "$IMAGE_TYPE",
  "logs_base64": "$all_logs",
  "recent_output": $step_outputs
}
EOF
)
    
    echo "$final_json"
}

# Main execution
main() {
    local run_id
    
    case "$MODE" in
        "trigger-only")
            log "Starting kernel test workflow trigger..."
            check_dependencies
            validate_build_id
            check_kernel_artifacts
            run_id=$(trigger_workflow_get_id)
            info "Workflow triggered. Use -w flag to wait for completion, or check GitHub Actions manually."
            log "Done!"
            ;;
            
        "wait-existing")
            check_dependencies
            run_id=$(get_latest_workflow_id)
            local output_path
            output_path=$(wait_and_write_json "$run_id" "$OUTPUT_FILE")
            echo "Results written to: $output_path"
            ;;
            
        "trigger-and-wait")
            check_dependencies
            validate_build_id
            run_id=$(trigger_workflow_get_id)
            local output_path
            output_path=$(wait_and_write_json "$run_id" "$OUTPUT_FILE")
            echo "Results written to: $output_path"
            ;;
            
        *)
            error "Invalid mode: $MODE"
            ;;
    esac
}

# Run if executed directly
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main "$@"
fi