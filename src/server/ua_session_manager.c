#include "ua_session_manager.h"
#include "ua_statuscodes.h"
#include "ua_util.h"

/**
 The functions in this file are not thread-safe. For multi-threaded access, a
 second implementation should be provided. See for example, how a nodestore
 implementation is choosen based on whether multithreading is enabled or not.
 */

struct session_list_entry {
    UA_Session session;
    LIST_ENTRY(session_list_entry) pointers;
};

UA_StatusCode UA_SessionManager_init(UA_SessionManager *sessionManager, UA_UInt32 maxSessionCount,
                                    UA_UInt32 maxSessionLifeTime, UA_UInt32 startSessionId) {
    LIST_INIT(&sessionManager->sessions);
    sessionManager->maxSessionCount = maxSessionCount;
    sessionManager->lastSessionId   = startSessionId;
    sessionManager->maxSessionLifeTime  = maxSessionLifeTime;
    sessionManager->currentSessionCount = 0;
    return UA_STATUSCODE_GOOD;
}

void UA_SessionManager_deleteMembers(UA_SessionManager *sessionManager) {
    struct session_list_entry *current;
    struct session_list_entry *next = LIST_FIRST(&sessionManager->sessions);
    while(next) {
        current = next;
        next = LIST_NEXT(current, pointers);
        LIST_REMOVE(current, pointers);
        if(current->session.channel)
            current->session.channel->session = UA_NULL; // the channel is no longer attached to a session
        UA_Session_deleteMembers(&current->session);
        UA_free(current);
    }
}

UA_StatusCode UA_SessionManager_getSessionById(UA_SessionManager *sessionManager, const UA_NodeId *sessionId,
                                               UA_Session **session) {
    if(sessionManager == UA_NULL) {
        *session = UA_NULL;
        return UA_STATUSCODE_BADINTERNALERROR;
    }

    struct session_list_entry *current = UA_NULL;
    LIST_FOREACH(current, &sessionManager->sessions, pointers) {
        if(UA_NodeId_equal(&current->session.sessionId, sessionId))
            break;
    }

    if(!current) {
        *session = UA_NULL;
        return UA_STATUSCODE_BADINTERNALERROR;
    }

    // Lifetime handling is not done here, but in a regular cleanup by the
    // server. If the session still exists, then it is valid.
    *session = &current->session;
    return UA_STATUSCODE_GOOD;
}

UA_StatusCode UA_SessionManager_getSessionByToken(UA_SessionManager *sessionManager, const UA_NodeId *token,
                                                  UA_Session **session) {
    if(sessionManager == UA_NULL) {
        *session = UA_NULL;
        return UA_STATUSCODE_BADINTERNALERROR;
    }

    struct session_list_entry *current = UA_NULL;
    LIST_FOREACH(current, &sessionManager->sessions, pointers) {
        if(UA_NodeId_equal(&current->session.authenticationToken, token))
            break;
    }

    if(!current) {
        *session = UA_NULL;
        return UA_STATUSCODE_BADINTERNALERROR;
    }

    // Lifetime handling is not done here, but in a regular cleanup by the
    // server. If the session still exists, then it is valid.
    *session = &current->session;
    return UA_STATUSCODE_GOOD;
}

/** Creates and adds a session. */
UA_StatusCode UA_SessionManager_createSession(UA_SessionManager *sessionManager, UA_SecureChannel *channel,
												const UA_CreateSessionRequest *request, UA_Session **session) {
    if(sessionManager->currentSessionCount >= sessionManager->maxSessionCount)
        return UA_STATUSCODE_BADTOOMANYSESSIONS;

    struct session_list_entry *newentry = UA_malloc(sizeof(struct session_list_entry));
    if(!newentry)
        return UA_STATUSCODE_BADOUTOFMEMORY;

    UA_Session_init(&newentry->session);
    newentry->session.sessionId = (UA_NodeId) {.namespaceIndex = 1, .identifierType = UA_NODEIDTYPE_NUMERIC,
                                               .identifier.numeric = sessionManager->lastSessionId++ };
    newentry->session.authenticationToken = (UA_NodeId) {.namespaceIndex = 1,
                                                         .identifierType = UA_NODEIDTYPE_NUMERIC,
                                                         .identifier.numeric = sessionManager->lastSessionId };
    newentry->session.channel = channel;
    newentry->session.timeout = (request->requestedSessionTimeout <= sessionManager->maxSessionLifeTime && request->requestedSessionTimeout>0) ? request->requestedSessionTimeout : sessionManager->maxSessionLifeTime;
    UA_Session_setExpirationDate(&newentry->session);

    sessionManager->currentSessionCount++;
    LIST_INSERT_HEAD(&sessionManager->sessions, newentry, pointers);
    *session = &newentry->session;
    return UA_STATUSCODE_GOOD;
}

UA_StatusCode UA_SessionManager_removeSession(UA_SessionManager *sessionManager, const UA_NodeId *sessionId) {
    struct session_list_entry *current = UA_NULL;
    LIST_FOREACH(current, &sessionManager->sessions, pointers) {
        if(UA_NodeId_equal(&current->session.sessionId, sessionId))
            break;
    }

    if(!current)
        return UA_STATUSCODE_BADINTERNALERROR;

    LIST_REMOVE(current, pointers);
    if(current->session.channel)
        current->session.channel->session = UA_NULL; // the channel is no longer attached to a session
    UA_Session_deleteMembers(&current->session);
    UA_free(current);
    return UA_STATUSCODE_GOOD;
}
